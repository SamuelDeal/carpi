#include "gpio_button_manager.hpp"

#include <poll.h>
#include <errno.h>
#include <signal.h>
#include <cstring>

#include "gpio_button.hpp"
#include "log.hpp"
#include "config.h"

const char GpioButtonManager::EXIT;
const char GpioButtonManager::BUTTON_CHANGED;
const char GpioButtonManager::BUTTON_LIST_CHANGED;

std::mutex GpioButtonManager::_mut;
GpioButtonManager* GpioButtonManager::_instance = NULL;
std::map<int, GpioButton*> GpioButtonManager::_btns;


bool GpioButtonManager::add(GpioButton *btn) {
    if(btn == NULL) {
        return false;
    }
    { //mutex scope
        const std::lock_guard<std::mutex> lock(_mut);
        int pin = btn->_pin;
        if(_btns.count(pin) != 0) {
            return false;
        }
        if(_instance == NULL) {
            _instance = new GpioButtonManager();
        }
        _btns[pin] = btn;
    }
    _instance->_pipe.send(GpioButtonManager::BUTTON_LIST_CHANGED);
    return true;
}

void GpioButtonManager::remove(GpioButton *btn) {
    if((btn == NULL) || (_instance == NULL)) {
        return;
    }
    GpioButtonManager *toDelete = NULL;
    { //mutex scope
        const std::lock_guard<std::mutex> lock(_mut);
        std::map<int, GpioButton*>::iterator i = _btns.find(btn->_pin);
        if(i == _btns.end()) {
            return;
        }
        _btns.erase(i);
        if(_btns.empty()) {
            toDelete = _instance;
            _instance = NULL;
        }
    }
    if(toDelete != NULL) {
        delete toDelete;
    }
    else {
        _instance->_pipe.send(GpioButtonManager::BUTTON_LIST_CHANGED);
    }
}

void GpioButtonManager::onButtonTimerChanged() {
    if(_instance == NULL) {
        return;
    }
   _instance->_pipe.send(GpioButtonManager::BUTTON_CHANGED);
}

GpioButtonManager::GpioButtonManager() {
    _timerFd = timerfd_create(CLOCK_MONOTONIC, 0);
    if(_timerFd == -1) {
        log(LOG_ERR, "unable to create timer for debouncing: %s",  strerror(errno));
        return;
    }
    _initTimerFd();
    pthread_create(&_thread, NULL, GpioButtonManager::_startRun, (void*)this);
}

GpioButtonManager::~GpioButtonManager() {
    _pipe.send(GpioButtonManager::EXIT);

    //wait the thread for 3sec
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 3;
    int joined = pthread_timedjoin_np(_thread, NULL, &ts);
    if(joined != 0) {
        log(LOG_ERR, "unable to join the button manager thread");
    }
    if(_timerFd != -1){
        close(_timerFd);
    }
}

void GpioButtonManager::_initTimerFd() {
    _interval.it_value.tv_sec = DEBOUNCE_READ_DELAY / 1000000;
    _interval.it_value.tv_nsec = (DEBOUNCE_READ_DELAY % 1000000)* 1000;
    _interval.it_interval.tv_sec = DEBOUNCE_READ_DELAY / 1000000;
    _interval.it_interval.tv_nsec = (DEBOUNCE_READ_DELAY % 1000000)* 1000;
    timerfd_settime(_timerFd, 0, &_interval, NULL);
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
    int oldstate;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
    ((GpioButtonManager*)manager)->_run();
    return NULL;
}

void GpioButtonManager::_run(){
    pollfd fdList[66];  //max 64 pins + event + timerfd
    _resetLocalList();
    int fdCount = _initFdList(fdList);

    while(1) {
        if(-1 == poll(fdList, fdCount, -1)) {
           log(LOG_ERR, "unable to listen buttons fd: %s", strerror(errno));
           return;
        }
        if(fdList[0].revents == POLLIN) {
            char msg = _pipe.read();
            if(msg == GpioButtonManager::EXIT){
                return;
            }
            else if(msg == GpioButtonManager::BUTTON_LIST_CHANGED) {
                _resetLocalList();
            }
            fdCount = _initFdList(fdList);
        }
        if(fdList[1].revents == POLLIN) { // tick
            _clearTimer(_timerFd);
            //all debouncing buttons have to read their value
            for(GpioButton* btn : _localBtns) {
                btn->_update();
            }
        }
        for(int i = 2; i < fdCount; i++) {
            if(fdList[i].revents == POLLIN) {
                _clearTimer(fdList[i].fd);
                _localBtns[i-2]->_onDelay();
            }
        }
    }
}


int GpioButtonManager::_initFdList(pollfd *fdList) {
    memset(fdList, 0, sizeof(pollfd)*66);
    int fdCount = 2;

    fdList[0].fd = _pipe.getReadFd();
    fdList[0].events = POLLIN;
    fdList[1].fd = _timerFd;
    fdList[1].events = POLLIN;

    for(GpioButton* btn : _localBtns) {
        if(btn->_timerFd != -1) {
            fdList[fdCount].fd = btn->_timerFd;
            fdList[fdCount].events = POLLIN;
            ++fdCount;
        }
    }
    return fdCount;
}

void GpioButtonManager::_resetLocalList() {
    std::lock_guard<std::mutex> lock(_mut);
    _localBtns.resize(_btns.size());

    int i = 0;
    for(std::pair<int, GpioButton*> pair : _btns) {
        _localBtns[i++] = pair.second;
    }
}

void GpioButtonManager::_clearTimer(int timerFd) {
    uint64_t data;
    read(timerFd, &data, sizeof(uint64_t));
}



