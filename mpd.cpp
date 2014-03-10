#include "mpd.hpp"

#include <mpd/client.h>
#include <mpd/status.h>
#include <mpd/entity.h>
#include <mpd/search.h>
#include <mpd/tag.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/timerfd.h>

#include "config.h"
#include "log.hpp"

const char Mpd::EXIT;
const char Mpd::PLAY_PAUSE;
const char Mpd::NEXT;
const char Mpd::PREV;
const char Mpd::IDLE;
const char Mpd::CONNECT;
const char Mpd::WAIT_RECONNECT;
const char Mpd::STATUS;

Mpd::Mpd() {
    _cnxDelay = MPD_RECONNECT_DELAY;
    _attemptCount = 0;
    _status = MPD_STATE_UNKNOWN;
    _queueLength = 0;
    _currentIndex = -1;
    _timerFd = -1;
    _conn = NULL;
    _cmds.push_back(Mpd::CONNECT);
    _cmds.push_back(Mpd::STATUS);
    pthread_create(&_thread, NULL, Mpd::_startRun, (void*)this);
}

Mpd::~Mpd() {
    _pipe.send(Mpd::EXIT);

    //wait thread for 3sec
    timespec ts;
    if(clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        log(LOG_ERR, "clock gettime failed");
        return;
    }
    ts.tv_sec += 3;
    int joined = pthread_timedjoin_np(_thread, NULL, &ts);
    if(joined != 0) {
        log(LOG_ERR, "unable to join the mpd thread");
    }
    if(_timerFd != -1){
        close(_timerFd);
    }
    if(_conn != NULL) {
        mpd_connection_free(_conn);
    }
}

void* Mpd::_startRun(void *mpd) {
    signal(SIGCHLD,SIG_DFL); // A child process dies
    signal(SIGTSTP,SIG_IGN); // Various TTY signals
    signal(SIGTTOU,SIG_IGN);
    signal(SIGTTIN,SIG_IGN);
    signal(SIGHUP, SIG_IGN); // Ignore hangup signal
    signal(SIGINT,SIG_IGN); // ignore SIGTERM
    signal(SIGQUIT,SIG_IGN); // ignore SIGTERM
    signal(SIGTERM,SIG_IGN); // ignore SIGTERM

    int oldstate;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
    ((Mpd*)mpd)->_run();
    return NULL;
}

void Mpd::_run() {
    while(true) {
        char cmd = _cmds.empty() ? Mpd::IDLE : _cmds.front();
        if(!_cmds.empty()) {
            _cmds.pop_front();
        }
        if(cmd == Mpd::EXIT) {
            return;
        }

        bool success = _execCmd(cmd);
        if(success) {
            if((cmd != Mpd::CONNECT) && (cmd != Mpd::WAIT_RECONNECT)) {
                _attemptCount = 0;
            }
        }
        else if(cmd == CONNECT) {
            mpd_connection_free(_conn);
            _conn = NULL;
            _cmds.push_front(Mpd::CONNECT);
            _cmds.push_front(Mpd::WAIT_RECONNECT);
        }
        else {
            if(_attemptCount >= 3) {
                log(LOG_ERR, "max attempt to execute mpd command failed, dropping command");
                _attemptCount = 0;
            }
            else{
                _cmds.push_front(cmd);
                ++_attemptCount;
            }
            if(!mpd_connection_clear_error(_conn)) {
                mpd_connection_free(_conn);
                _conn = NULL;
                _cmds.push_front(Mpd::CONNECT);
            }
        }
    }
}


bool Mpd::_execCmd(char cmd) {
    switch(cmd) {
        case Mpd::STATUS:
            return _getStatus();

        case Mpd::NEXT:
            return _playNext();

        case Mpd::CONNECT:
            return _connect();

        case Mpd::WAIT_RECONNECT:
            return _waitReconnect();

        case Mpd::IDLE:
            return _idle();

        default:
            log(LOG_ERR, "unknown mpd command: %d", cmd);
            return true;
    }
}

bool Mpd::_connect() {
    _conn = mpd_connection_new(NULL, 0, 30000);
    if(mpd_connection_get_error(_conn) == MPD_ERROR_SUCCESS) {
        _cnxDelay = MPD_RECONNECT_DELAY;
        log(LOG_INFO, "mpd connection established");
        if(_timerFd != -1){
            close(_timerFd);
            _timerFd = -1;
        }
        return true;
    }
    log(LOG_ERR, "mpd conmection failed: %s", mpd_connection_get_error_message(_conn));
    _cnxDelay *= MPD_RECONNECT_ACCEL;
    if(_cnxDelay > MPD_RECONNECT_MAXDELAY) {
        _cnxDelay = MPD_RECONNECT_MAXDELAY;
    }
    return false;
}

bool Mpd::_waitReconnect() {
    if(_timerFd == -1) {
        _timerFd = timerfd_create(CLOCK_MONOTONIC, 0);
    }
    itimerspec interval;
    interval.it_value.tv_sec = _cnxDelay / 1000000;
    interval.it_value.tv_nsec = (_cnxDelay % 1000000)* 1000;
    interval.it_interval.tv_sec = 0;
    interval.it_interval.tv_nsec = 0;
    timerfd_settime(_timerFd, 0, &interval, NULL);
    while(!_waitEvent(_timerFd)){
        if(_cmds.front() == Mpd::EXIT) {
            return true;
        }
    }
    uint64_t unused;
    read(_timerFd, &unused, sizeof(uint64_t));
    return true;
}

bool Mpd::_idle() {
    if(!mpd_send_idle(_conn) || mpd_connection_get_error(_conn) != MPD_ERROR_SUCCESS) {
        log(LOG_ERR, "idle mode failed: %s", mpd_connection_get_error_message(_conn));
        return false;
    }
    _waitEvent(mpd_connection_get_fd(_conn));
    if(_cmds.front() == Mpd::EXIT) {
        return true;
    }
    int changes = (int)mpd_recv_idle(_conn, false);
    if(changes == 0 || mpd_connection_get_error(_conn) != MPD_ERROR_SUCCESS) {
        log(LOG_ERR, "idle mode failed: %s", mpd_connection_get_error_message(_conn));
        return false;
    }
    if(!mpd_response_finish(_conn)) {
        log(LOG_ERR, "idle mode failed: %s", mpd_connection_get_error_message(_conn));
        return false;
    }
    if((changes & MPD_IDLE_QUEUE) == MPD_IDLE_QUEUE || (changes & MPD_IDLE_PLAYER) == MPD_IDLE_PLAYER) {
       _cmds.push_front(Mpd::STATUS);
    }
    return true;
}

bool Mpd::_waitEvent(int contextFd) {
    pollfd fds[2];
    memset(fds, 0, sizeof(fds));
    fds[0].fd = _pipe.getReadFd();
    fds[0].events = POLLIN;
    fds[1].fd = contextFd;
    fds[1].events = POLLIN;
    poll(fds, 2, -1);
    if(fds[0].revents == POLLIN) {
      _cmds.push_back(_pipe.read());
    }
    if(fds[1].revents == POLLIN) {
        return true;
    }
    return false;
}

void Mpd::next(){
    _pipe.send(Mpd::NEXT);
}


bool Mpd::_playNext() {
    if(_queueLength == 0){
        return true;
    }
    if((_status == MPD_STATE_PLAY) && (_queueLength == _currentIndex - 1)) {
        return true;
    }
    int next = _currentIndex +1;
    if(next + 1 >= _queueLength) {
        next = _queueLength -1;
    }
    if(!mpd_run_play_pos(_conn, next) || mpd_connection_get_error(_conn) != MPD_ERROR_SUCCESS) {
        log(LOG_ERR, "playing next song failed: %s", mpd_connection_get_error_message(_conn));
        return false;
    }
    _currentIndex = next;
    return true;
}

bool Mpd::_getStatus() {
    mpd_send_status(_conn);
    mpd_status* status = mpd_recv_status(_conn);
    if(status == NULL) {
        log(LOG_ERR, "mpd status failed: %s", mpd_connection_get_error_message(_conn));
        return false;
    }
    _status = mpd_status_get_state(status);
    _queueLength = mpd_status_get_queue_length(status);
    if(_status == MPD_STATE_PLAY || _status == MPD_STATE_PAUSE) {
        _currentIndex = mpd_status_get_song_pos(status);
    }
    else {
        _currentIndex = -1;
    }
    mpd_status_free(status);
    if(!mpd_response_finish(_conn)) {
        log(LOG_ERR, "mpd status failed: %s", mpd_connection_get_error_message(_conn));
        return false;
    }
    return true;
}

/*
        if(_idle) {
            poll(fds, 1, -1);
            if(fds[0].revents == POLLIN) { //exit cmd;
                uint64_t msg = readEvent(_efd);
                if(msg == Mpd::EXIT){
                    exit = true;
                }
                else {
                    std::lock_guard<std::mutex> lock(_mut);
                    _cmds.push_front(Mpd::statusCmd);
                    _cmds.push_front(Mpd::noIdleCmd);
                }
            }
        }
        else {
            cnxDelay = MPD_RECONNECT_DELAY;
           if(!exit) {
                log(LOG_ERR, "mpd conmection lost: %s", mpd_connection_get_error_message(_conn));
            }
            mpd_connection_free(_conn);
        }
    }
}
*/

/*

static void
print_tag(const struct mpd_song *song, enum mpd_tag_type type,
        const char *label)
{
unsigned i = 0;
      const char *value;

      while ((value = mpd_song_get_tag(song, type, i++)) != NULL)
            printf("%s: %s\n", label, value);
}

int main(int argc, char ** argv) {
      struct mpd_connection *conn;

      conn = mpd_connection_new(NULL, 0, 30000);

      if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
            fprintf(stderr,"%s\n", mpd_connection_get_error_message(conn));
            mpd_connection_free(conn);
            return -1;
      }

      {
            int i;
            for(i=0;i<3;i++) {
                  printf("version[%i]: %i\n",i,
                         mpd_connection_get_server_version(conn)[i]);
            }
      }

      if(argc==1) {
            struct mpd_status * status;
            struct mpd_song *song;
            const struct mpd_audio_format *audio_format;

            mpd_command_list_begin(conn, true);
            mpd_send_status(conn);
            mpd_send_current_song(conn);
            mpd_command_list_end(conn);

            status = mpd_recv_status(conn);
            if (status == NULL) {
                  fprintf(stderr,"%s\n", mpd_connection_get_error_message(conn));
                  mpd_connection_free(conn);
                  return -1;
            }

            printf("volume: %i\n", mpd_status_get_volume(status));
            printf("repeat: %i\n", mpd_status_get_repeat(status));
            printf("queue version: %u\n", mpd_status_get_queue_version(status));
            printf("queue length: %i\n", mpd_status_get_queue_length(status));
            if (mpd_status_get_error(status) != NULL)
                  printf("error: %s\n", mpd_status_get_error(status));


                  printf("elaspedTime: %i\n",mpd_status_get_elapsed_time(status));
                  printf("elasped_ms: %u\n", mpd_status_get_elapsed_ms(status));
                  printf("totalTime: %i\n", mpd_status_get_total_time(status));
                  printf("bitRate: %i\n", mpd_status_get_kbit_rate(status));
            }

            audio_format = mpd_status_get_audio_format(status);
            if (audio_format != NULL) {
                  printf("sampleRate: %i\n", audio_format->sample_rate);
                  printf("bits: %i\n", audio_format->bits);
                  printf("channels: %i\n", audio_format->channels);
            }

            if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
                  fprintf(stderr,"%s\n", mpd_connection_get_error_message(conn));
                  mpd_connection_free(conn);
                  return -1;
            }

            mpd_response_next(conn);

            while ((song = mpd_recv_song(conn)) != NULL) {
                  printf("uri: %s\n", mpd_song_get_uri(song));
                  print_tag(song, MPD_TAG_ARTIST, "artist");
                  print_tag(song, MPD_TAG_ALBUM, "album");
                  print_tag(song, MPD_TAG_TITLE, "title");
                  print_tag(song, MPD_TAG_TRACK, "track");
                  print_tag(song, MPD_TAG_NAME, "name");
                  print_tag(song, MPD_TAG_DATE, "date");

                  if (mpd_song_get_duration(song) > 0) {
                        printf("time: %u\n", mpd_song_get_duration(song));
                  }

                  printf("pos: %u\n", mpd_song_get_pos(song));

                  mpd_song_free(song);
            }

            if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
                  fprintf(stderr,"%s\n", mpd_connection_get_error_message(conn));
                  mpd_connection_free(conn);
                  return -1;
            }

            if (!mpd_response_finish(conn)) {
                  fprintf(stderr,"%s\n", mpd_connection_get_error_message(conn));
                  mpd_connection_free(conn);
                  return -1;
            }


      }
      else if(argc==3 && strcmp(argv[1],"lsinfo")==0) {
            struct mpd_entity * entity;

            if (!mpd_send_list_meta(conn,argv[2])) {
                  fprintf(stderr,"%s\n", mpd_connection_get_error_message(conn));
                  mpd_connection_free(conn);
                  return -1;
            }

            while ((entity = mpd_recv_entity(conn)) != NULL) {
                  const struct mpd_song *song;
                  const struct mpd_directory *dir;
                  const struct mpd_playlist *pl;

                  switch (mpd_entity_get_type(entity)) {
                  case MPD_ENTITY_TYPE_UNKNOWN:
                        break;

                  case MPD_ENTITY_TYPE_SONG:
                        song = mpd_entity_get_song(entity);
                        printf("uri: %s\n", mpd_song_get_uri(song));
                        print_tag(song, MPD_TAG_ARTIST, "artist");
                        print_tag(song, MPD_TAG_ALBUM, "album");
                        print_tag(song, MPD_TAG_TITLE, "title");
                        print_tag(song, MPD_TAG_TRACK, "track");
                        break;

                  case MPD_ENTITY_TYPE_DIRECTORY:
                        dir = mpd_entity_get_directory(entity);
                        printf("directory: %s\n", mpd_directory_get_path(dir));
                        break;

                  case MPD_ENTITY_TYPE_PLAYLIST:
                        pl = mpd_entity_get_playlist(entity);
                        printf("playlist: %s\n",
                               mpd_playlist_get_path(pl));
                        break;
                  }

                  mpd_entity_free(entity);
            }

            if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
                  fprintf(stderr, "%s\n",
                        mpd_connection_get_error_message(conn));
                  mpd_connection_free(conn);
                  return -1;
            }

            if (!mpd_response_finish(conn)) {
                  fprintf(stderr, "%s\n", mpd_connection_get_error_message(conn));
                  mpd_connection_free(conn);
                  return -1;
            }
      }
      else if(argc==2 && strcmp(argv[1],"artists")==0) {
            struct mpd_pair *pair;

            if (!mpd_search_db_tags(conn, MPD_TAG_ARTIST) ||
                !mpd_search_commit(conn)) {
                  fprintf(stderr,"%s\n", mpd_connection_get_error_message(conn));
                  mpd_connection_free(conn);
                  return -1;
            }

            while ((pair = mpd_recv_pair_tag(conn,
                                     MPD_TAG_ARTIST)) != NULL) {
                  printf("%s\n", pair->value);
                  mpd_return_pair(conn, pair);
            }

             if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
                  fprintf(stderr,"%s\n", mpd_connection_get_error_message(conn));
                 mpd_connection_free(conn);
                  return -1;
            }

            if (!mpd_response_finish(conn)) {
                  fprintf(stderr,"%s\n", mpd_connection_get_error_message(conn));
                  mpd_connection_free(conn);
                  return -1;
            }
      }

      mpd_connection_free(conn);

      return 0;
}

*/

