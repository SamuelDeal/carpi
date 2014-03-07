#include "led.hpp"

#include <signal.h>
#include <errno.h>
#include <cstring>

#include "log.hpp"

const char Led::ON;
const char Led::OFF;
const char Led::BLINK_SLOWLY;
const char Led::BLINK_QUICKLY;
const char Led::QUIT;
const long Led::SLOW_TIME;
const long Led::QUICK_TIME;

Led::Led(int ledPin): _pin(ledPin) {
    _isOn = false;
    _status = Led::OFF;
}

Led::~Led() {
    _stopBlinking();
    _light(false);
    _pin.write(Gpio::low);
}

void Led::on() {
    const std::lock_guard<std::mutex> lock(_mut);
    if(_status == Led::ON){
        return;
    }
    _stopBlinking();
    _status = Led::ON;
    _light(true);
}

void Led::off() {
    const std::lock_guard<std::mutex> lock(_mut);
    if(_status == Led::OFF){
        return;
    }
    _stopBlinking();
    _status = Led::OFF;
    _light(false);
}

void Led::blinkSlowly() {
    const std::lock_guard<std::mutex> lock(_mut);
    if(_status == Led::BLINK_SLOWLY){
        return;
    }
    if(_blinking()){
        _status = Led::BLINK_SLOWLY;
        _pipe.send(Led::BLINK_SLOWLY);
    }
    else{
        _status = Led::BLINK_SLOWLY;
        pthread_create(&_thread, NULL, Led::_startBlinking, (void*)this);
    }
}

void Led::blinkQuickly() {
    const std::lock_guard<std::mutex> lock(_mut);
    if(_status == Led::BLINK_QUICKLY){
        return;
    }
    if(_blinking()){
        _status = Led::BLINK_QUICKLY;
        _pipe.send(Led::BLINK_QUICKLY);
    }
    else{
        _status = Led::BLINK_QUICKLY;
        pthread_create(&_thread, NULL, Led::_startBlinking, (void*)this);
    }
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
    _pipe.send(Led::QUIT);

    //wait thread for 3sec
    timespec ts;
    if(clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        log(LOG_ERR, "clock gettime failed");
    }
    else{ ts.tv_sec += 3;
        int joined = pthread_timedjoin_np(_thread, NULL, &ts);
        if(joined != 0) {
            log(LOG_ERR, "unable to join the thread");
        }
    }
}

void* Led::_startBlinking(void*led){
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

    ((Led*)led)->_blink();
    return NULL;
}

void Led::_blink(){
    long time = _status == Led::BLINK_SLOWLY ? Led::SLOW_TIME : Led::QUICK_TIME;
    _light(!_isOn);
    bool exit = false;
    while(!exit) {
        fd_set rfds;
        struct timeval tv;
        int retval;

        FD_ZERO(&rfds);
        FD_SET(_pipe.getReadFd(), &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = time;

        retval = select(_pipe.getReadFd()+1, &rfds, NULL, NULL, &tv);
        if(retval == -1) {
            log(LOG_ERR, "select() failed");
            exit = true;
        }
        else if (!retval) {  // timeout
            time = _status == Led::BLINK_SLOWLY ? Led::SLOW_TIME : Led::QUICK_TIME;
            _light(!_isOn);
        }
        else {
            char value = _pipe.read();
            if(value == 0) {
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



