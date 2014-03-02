#include "gpio.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <cstring>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/timerfd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <map>
#include <list>
#include <mutex>
#include <signal.h>
#include "log.hpp"
#include "config.h"
#include "gpio_core.hpp"
#include "fd_utils.hpp"

class GpioButtonManager {
    public:
        static bool add(GpioButton *);
        static void remove(GpioButton *);

    protected:
        static GpioButtonManager* _instance;
        static std::mutex _mut;
        static std::map<int, GpioButton*> _btns;

        static const uint64_t EXIT = 1;
        static const uint64_t BUTTON_CHANGED = 2;

        GpioButtonManager();
        ~GpioButtonManager();

        static void* _startRun(void* manager);
        void _run();

        int _initFdList(fd_set*);

        int _inFd;
        int _timerFd;
        itimerspec _interval;
        pthread_t _thread;
};





#define DEBOUNCE_TIME         30000  //latency, usec
#define DEBOUNCE_READ_DELAY   10000  //read delay, usec
#define INTEGRATOR_MAXIMUM    (DEBOUNCE_TIME / DEBOUNCE_READ_DELAY)
#define BUTTON_DELAY          800000 // before rebounce or long press, usec
#define BUTTON_MIN_DELAY      80000  // min time before rebounce, usec
#define REBOUNCE_ACCEL        0.6


const uint64_t GpioButton::PRESS;
const uint64_t GpioButton::RELEASE;
const uint64_t GpioButton::LONG_PRESS;
const uint64_t GpioButton::LONG_RELEASE;


GpioButton::GpioButton(int pin, bool rebounce, bool defaultHigh) {
    _pin = pin;
    _initFailed = true;
    _sysFd = -1;
    GpioCore::get().setPinMode(pin, Gpio::input);
    _status = defaultHigh ? Gpio::high : Gpio::low;
    _integrator = defaultHigh ? INTEGRATOR_MAXIMUM : 0;
    _defaultHigh = defaultHigh;
    _stable = 0;
    _rebounce = rebounce;
    _debouncing = false;
    _timerFd = -1;
    _long = false;

    _eventFd = eventfd(0, EFD_NONBLOCK);
    if(_eventFd == -1) {
        log(LOG_ERR, "unable to create eventfd for button pi %d: %s", pin, strerror(errno));
        return;
    }
    _sysFd = GpioCore::get().getPinFd(pin);
    if(_eventFd == -1) {
        return;
    }
    _initFailed = !GpioButtonManager::add(this);
}

GpioButton::~GpioButton() {
    if(!_initFailed) {
        GpioButtonManager::remove(this);
    }
    if(_timerFd != -1) {
        close(_timerFd);
    }
    if(_eventFd != -1) {
        close(_eventFd);
    }
    if(_sysFd != -1) {
        close(_sysFd);
    }
    GpioCore::get().unexportPin(_pin);
}

bool GpioButton::isValid() const {
    return !_initFailed;
}

int GpioButton::getEventFd() const {
    return _eventFd;
}

Gpio::Value GpioButton::_integrate(Gpio::Value input) {
    // integrator algo explaination:
    //
    // real signal 0000111111110000000111111100000000011111111110000000000111111100000
    //
    // corrupted   0100111011011001000011011010001001011100101111000100010111011100010
    // integrator  0100123233233212100012123232101001012321212333210100010123233321010
    // output      0000001111111111100000001111100000000111111111110000000001111111000
    // stable      1012001012012001012300101010010120100100101233001012301001012300101

    Gpio::Value output; // Cleaned-up version of the input signal

    if(input == Gpio::low) {
        --_integrator;
    }
    else {
        ++_integrator;
    }

    if(_integrator <= 0) {
        output = Gpio::low;
        _integrator = 0;
    }
    else if(_integrator >= INTEGRATOR_MAXIMUM) {
        output = Gpio::high;
        _integrator = INTEGRATOR_MAXIMUM;
    }
    else {
        output = _status;
    }

    if(input != output) {
        _stable = 0;
    }
    else if(_stable < INTEGRATOR_MAXIMUM){
        ++_stable;
    }
    return output;
}

long computeNextDelay(long currentDelay){
    long result = currentDelay * REBOUNCE_ACCEL;
    if(result < BUTTON_MIN_DELAY * 1000){
        result = BUTTON_MIN_DELAY * 1000;
    }
    return result;
}

void GpioButton::_update() {
    Gpio::Value output = _integrate(GpioCore::get().readPin(_pin));
    if(output != _status) {
        _status = output;
        _debouncing = false;
        _stable = 0;
        if(((_status == Gpio::high) && !_defaultHigh) || ((_status == Gpio::low) && _defaultHigh)) {
            sendEvent(_eventFd, GpioButton::PRESS);

            log(LOG_INFO, "button: init timer");
            _interval.it_value.tv_sec = 0;
            _interval.it_value.tv_nsec = BUTTON_DELAY * 1000;
            _interval.it_interval.tv_sec = 0;
            _interval.it_interval.tv_nsec = _rebounce ? computeNextDelay(BUTTON_DELAY * 1000) : (BUTTON_DELAY * 1000);
            _timerFd = timerfd_create(CLOCK_MONOTONIC, 0);
            timerfd_settime(_timerFd, 0, &_interval, NULL);
            if(_timerFd == -1) {
                log(LOG_ERR, "unable to create timer for debouncing: %s",  strerror(errno));
            }
        }
        else {
            sendEvent(_eventFd, (_long ? GpioButton::LONG_RELEASE : GpioButton::RELEASE));
            log(LOG_INFO, "button: close timer 1");
            close(_timerFd);
            _timerFd = -1;
        }
        _long = false;
    }
    else if(_stable >= INTEGRATOR_MAXIMUM) {
        _debouncing = false;
        _stable = 0;

        log(LOG_INFO, "button: close timer 2");
        close(_timerFd);
        _timerFd = -1;
    }
}

void GpioButton::_onDelay() {
     sendEvent(_eventFd, (_rebounce ? GpioButton::PRESS : GpioButton::LONG_PRESS));

     if(_rebounce) {
         long next = computeNextDelay(_interval.it_value.tv_nsec);
         _interval.it_value.tv_sec = 0;
         _interval.it_value.tv_nsec = next;
         _interval.it_interval.tv_sec = 0;
         _interval.it_interval.tv_nsec = computeNextDelay(next);
         timerfd_settime(_timerFd, 0, &_interval, NULL);
     }
     else {
         log(LOG_INFO, "button: close timer 3");
         close(_timerFd);
         _timerFd = -1;
     }
     _long = true;
}

const uint64_t GpioButtonManager::EXIT;
const uint64_t GpioButtonManager::BUTTON_CHANGED;

std::mutex GpioButtonManager::_mut;
GpioButtonManager* GpioButtonManager::_instance = NULL;
std::map<int, GpioButton*> GpioButtonManager::_btns;


bool GpioButtonManager::add(GpioButton *btn) {
    log(LOG_INFO, "button add");
    if(btn == NULL) {
        return false;
    }
    const std::lock_guard<std::mutex> lock(_mut);
    int pin = btn->_pin;
    if(_btns.count(pin) != 0) {
        return false;
    }
    if(_instance == NULL) {
        _instance = new GpioButtonManager();
    }
    _btns[pin] = btn;
    sendEvent(_instance->_inFd, GpioButtonManager::BUTTON_CHANGED);
    return true;
}

void GpioButtonManager::remove(GpioButton *btn) {
    log(LOG_INFO, "button remove");
    if((btn == NULL) || (_instance == NULL)) {
        return;
    }
    const std::lock_guard<std::mutex> lock(_mut);
    std::map<int, GpioButton*>::iterator i = _btns.find(btn->_pin);
    if(i == _btns.end()) {
        return;
    }
    _btns.erase(i);
    if(_btns.empty()) {
        delete _instance;
        _instance = NULL;
    }
    else {
        sendEvent(_instance->_inFd, GpioButtonManager::BUTTON_CHANGED);
    }
}

GpioButtonManager::GpioButtonManager() {
    _inFd = eventfd(0, EFD_NONBLOCK);
    if(_inFd == -1) {
        log(LOG_ERR, "unable to call eventfd to GPIO button add/remove: %s",  strerror(errno));
        return;
    }

    _interval.it_value.tv_sec = 0;
    _interval.it_value.tv_nsec = DEBOUNCE_READ_DELAY * 1000;
    _interval.it_interval.tv_sec = 0;
    _interval.it_interval.tv_nsec = DEBOUNCE_READ_DELAY * 1000;
    _timerFd = timerfd_create(CLOCK_MONOTONIC, 0);
    timerfd_settime(_timerFd, 0, &_interval, NULL);
    if(_timerFd == -1) {
        log(LOG_ERR, "unable to create timer for debouncing: %s",  strerror(errno));
        return;
    }
    pthread_create(&_thread, NULL, GpioButtonManager::_startRun, (void*)this);
}

GpioButtonManager::~GpioButtonManager() {
    log(LOG_INFO, "btn mgnt destructor, %d", _inFd);
    if(_inFd != -1) {
        log(LOG_INFO, "btn mgnt destructor, write");
        sendEvent(_instance->_inFd, GpioButtonManager::EXIT);
        log(LOG_INFO, "btn mgnt destructor, join");
        pthread_join(_thread, NULL);
        log(LOG_INFO, "btn mgnt destructor, close");
        close(_inFd);
        log(LOG_INFO, "btn mgnt destructor, closed");
    }
    if(_timerFd != -1){
        close(_timerFd);
    }
}

void* GpioButtonManager::_startRun(void *manager) {
    signal(SIGCHLD,SIG_DFL); // A child process dies
    signal(SIGTSTP,SIG_IGN); // Various TTY signals
    signal(SIGTTOU,SIG_IGN);
    signal(SIGTTIN,SIG_IGN);
    signal(SIGHUP, SIG_IGN); // Ignore hangup signal
    signal(SIGINT,SIG_IGN); // ignore SIGTERM
    signal(SIGQUIT,SIG_IGN); // ignore SIGTERM
    signal(SIGTERM,SIG_IGN); // ignore SIGTERM

    ((GpioButtonManager*)manager)->_run();
    return NULL;
}

void GpioButtonManager::_run(){
    while(1) {
        log(LOG_INFO, "button mgnt loop");

        fd_set fdList;
        int maxFd = _initFdList(&fdList);

        if(-1 == select(maxFd+1, &fdList, NULL, NULL, NULL)) {
           log(LOG_ERR, "unable to listen buttons fd: %s", strerror(errno));
           return;
        }
        if(FD_ISSET(_inFd, &fdList)) {
            uint64_t msg = readEvent(_inFd);
            log(LOG_INFO, "button mgnt loop, infd: %llu, %llu, %llu", msg, GpioButtonManager::EXIT, GpioButtonManager::BUTTON_CHANGED);
            if(msg == GpioButtonManager::EXIT){
            log(LOG_INFO, "button mgnt loop, EXIT %llu", msg);
                return;
            }
            else if(msg == GpioButtonManager::BUTTON_CHANGED) {
            log(LOG_INFO, "button mgnt loop, BUTTON_CHANGED %llu", msg);
            }
            else {
            log(LOG_INFO, "button mgnt loop, ERROR %llu", msg);
             //else button changed, nothing to do
            }
        }
        else if(FD_ISSET(_timerFd, &fdList)) { // tick read
            log(LOG_INFO, "button mgnt loop, main timer");
            const std::lock_guard<std::mutex> lock(_mut);
            clearReads(_timerFd);
            //all debouncing buttons have to read their value
            for(std::pair<int, GpioButton*> pair : _btns) {
                if(pair.second->_debouncing) {
                    pair.second->_update();
                }
            }
        }
        else {
            const std::lock_guard<std::mutex> lock(_mut);
            for(std::pair<int, GpioButton*> pair : _btns) {
                if(FD_ISSET(pair.second->_sysFd, &fdList)) {
                    log(LOG_INFO, "button mgnt loop, sysfd");
                    pair.second->_debouncing = true;
                    clearReads(pair.second->_sysFd);
                }
                else if(FD_ISSET(pair.second->_timerFd, &fdList)) {
                    log(LOG_INFO, "button mgnt loop, btn timer");
                    pair.second->_onDelay();
                    clearReads(pair.second->_timerFd);
                }
            }
        }
    }
}


int GpioButtonManager::_initFdList(fd_set *fdList) {
    int maxFd = _inFd;
    FD_ZERO(fdList);
    FD_SET(_inFd, fdList);
    bool debouncing = false;
    {  //mutex scope
        const std::lock_guard<std::mutex> lock(_mut);
        for(std::pair<int, GpioButton*> pair : _btns) {
            if(pair.second->_debouncing) {
                debouncing = true;
            }
            else {
                int fd = pair.second->_sysFd;
                if(fd > maxFd) {
                    maxFd = fd;
                }
                clearReads(fd);
                FD_SET(fd, fdList);
                fd = pair.second->_timerFd;
                if(fd != -1) {
                    if(fd > maxFd) {
                        maxFd = fd;
                    }
                    FD_SET(fd, fdList);
                }
            }
        }
    }
    if(debouncing) {
        if(_timerFd > maxFd) {
            maxFd = _timerFd;
        }
        FD_SET(_timerFd, fdList);
    }
    return maxFd;
}
