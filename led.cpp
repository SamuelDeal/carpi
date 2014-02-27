#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <cstring>

#include "led.hpp"
#include "log.hpp"

const uint64_t Led::ON;
const uint64_t Led::OFF;
const uint64_t Led::BLINK_SLOWLY;
const uint64_t Led::BLINK_QUICKLY;
const uint64_t Led::QUIT;
const long Led::SLOW_TIME;
const long Led::QUICK_TIME;

Led::Led(int ledPin): _pin(ledPin), _mut(PTHREAD_MUTEX_INITIALIZER) {
    _isOn = false;
    _status = Led::OFF;
}

Led::~Led() {
    _stopBlinking();
    _light(false);
    _pin.write(Gpio::low);
}

void Led::on() {
    pthread_mutex_lock(&_mut);
    if(_status == Led::ON){
        pthread_mutex_unlock(&_mut);
        return;
    }
    _stopBlinking();
    _status = Led::ON;
    pthread_mutex_unlock(&_mut);
    _light(true);
}

void Led::off() {
    pthread_mutex_lock(&_mut);
    if(_status == Led::OFF){
        pthread_mutex_unlock(&_mut);
        return;
    }
    _stopBlinking();
    _status = Led::OFF;
    pthread_mutex_unlock(&_mut);
    _light(false);
}

void Led::blinkSlowly() {
    pthread_mutex_lock(&_mut);
    if(_status == Led::BLINK_SLOWLY){
        pthread_mutex_unlock(&_mut);
        return;
    }
    if(_blinking()){
        _status = Led::BLINK_SLOWLY;
        size_t s = write(_efd, &Led::BLINK_SLOWLY, sizeof(uint64_t));
        if(s != sizeof(uint64_t)){
            log(LOG_ERR, "write failed");
        }
    }
    else{
        _status = Led::BLINK_SLOWLY;
        _efd = eventfd(0, 0);
        if(_efd == -1) {
            log(LOG_ERR, "eventfd failed: %s", strerror(errno));
            pthread_mutex_unlock(&_mut);
            return;
        }
        int result = pthread_create(&_thread, NULL, Led::_startBlinking, (void*)this);
    }
    pthread_mutex_unlock(&_mut);
}

void Led::blinkQuickly() {
    pthread_mutex_lock(&_mut);
    if(_status == Led::BLINK_QUICKLY){
        pthread_mutex_unlock(&_mut);
        return;
    }
    if(_blinking()){
        _status = Led::BLINK_QUICKLY;
        size_t s = write(_efd, &Led::BLINK_QUICKLY, sizeof(uint64_t));
        if(s != sizeof(uint64_t)){
            log(LOG_ERR, "write failed");
        }
    }
    else{
        _status = Led::BLINK_QUICKLY;
        _efd = eventfd(0, 0);
        if(_efd == -1) {
            log(LOG_ERR, "eventfd failed: %s", strerror(errno));
            pthread_mutex_unlock(&_mut);
            return;
        }
        int result = pthread_create(&_thread, NULL, Led::_startBlinking, (void*)this);
    }
    pthread_mutex_unlock(&_mut);
}

void Led::_light(bool value){
    if(_isOn == value){
       return;
    }
    _pin.write(value ? Gpio::high : Gpio::low);
    _isOn = value;
}

bool Led::_blinking(){
    return (_status == Led::BLINK_SLOWLY || _status == Led::BLINK_QUICKLY);
}

void Led::_stopBlinking() {
    if(!_blinking()){
        return;
    }
    size_t s = write(_efd, &Led::QUIT, sizeof(uint64_t));
    if(s != sizeof(uint64_t)){
        log(LOG_ERR, "write failed");
    }
    else{
        pthread_join(_thread, NULL);
    }
}

void* Led::_startBlinking(void*led){
    ((Led*)led)->_blink();
    return NULL;
}

void Led::_blink(){
    signal(SIGCHLD,SIG_DFL); // A child process dies
    signal(SIGTSTP,SIG_IGN); // Various TTY signals
    signal(SIGTTOU,SIG_IGN);
    signal(SIGTTIN,SIG_IGN);
    signal(SIGHUP, SIG_IGN); // Ignore hangup signal
    signal(SIGINT,SIG_IGN); // ignore SIGTERM
    signal(SIGQUIT,SIG_IGN); // ignore SIGTERM
    signal(SIGTERM,SIG_IGN); // ignore SIGTERM

    long time = _status == Led::BLINK_SLOWLY ? Led::SLOW_TIME : Led::QUICK_TIME;
    _light(!_isOn);
    bool exit = false;
    while(!exit){
        fd_set rfds;
        struct timeval tv;
        int retval;

        FD_ZERO(&rfds);
        FD_SET(_efd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = time;

        retval = select(_efd+1, &rfds, NULL, NULL, &tv);
        if(retval == -1) {
            log(LOG_ERR, "select() failed");
            exit = true;
        }
        else if (!retval) {  // timeout
            time = _status == Led::BLINK_SLOWLY ? Led::SLOW_TIME : Led::QUICK_TIME;
            _light(!_isOn);
        }
        else {
            uint64_t value;
            size_t s = read(_efd, &value, sizeof(uint64_t));
            if(s != sizeof(uint64_t)){
                log(LOG_ERR, "read() failed");
                exit = true;
            }
            else if(value == Led::QUIT){
	        exit = true;
            }
	    else{
	        time = _status == Led::BLINK_SLOWLY ? Led::SLOW_TIME : Led::QUICK_TIME;
                if(tv.tv_usec > time){
                    time = tv.tv_usec - time;
                }
                else{
                    _light(!_isOn);
                }
            }
        }
    }
}
