#ifndef _MPD_HPP
#define _MPD_HPP

#include <mpd/connection.h>
#include <mpd/status.h>
#include <pthread.h>
#include <deque>
#include <poll.h>

#include "pipe.hpp"

class Mpd {
    public:
        Mpd();
        ~Mpd();

        void playOrPause();
        bool isQueueEmpty();
        void next();
        void prev();

    protected:
        static const char EXIT = 1;
        static const char PLAY_PAUSE = 2;
        static const char NEXT = 3;
        static const char PREV = 4;
        static const char IDLE = 5;
        static const char NO_IDLE = 6;
        static const char CONNECT = 7;
        static const char WAIT_RECONNECT = 8;
        static const char STATUS = 9;

        pthread_t _thread;
        Pipe _pipe;
        mpd_connection *_conn;
        std::deque<char> _cmds;
        int _queueLength;
        int _currentIndex;
        int _cnxDelay;
        mpd_state _status;
        int _attemptCount;
        int _timerFd;

        static void* _startRun(void*);
        void _run();
        bool _execCmd(char cmd);
        bool _connect();
        bool _waitReconnect();
        bool _idle();
        bool _noIdle();
        bool _getStatus();
        bool _playNext();
};

#endif // _MPD_HPP

