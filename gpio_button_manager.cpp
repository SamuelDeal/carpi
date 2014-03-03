#include "gpio_button_manager.hpp"

#include <sys/eventfd.h>
#include <poll.h>
#include <errno.h>
#include <signal.h>
#include <cstring>

#include "gpio_button.hpp"
#include "log.hpp"
#include "fd_utils.hpp"
#include "config.h"

const uint64_t GpioButtonManager::EXIT;
const uint64_t GpioButtonManager::BUTTON_CHANGED;
const uint64_t GpioButtonManager::BUTTON_LIST_CHANGED;

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
    sendEvent(_instance->_inFd, GpioButtonManager::BUTTON_LIST_CHANGED);
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
        sendEvent(_instance->_inFd, GpioButtonManager::BUTTON_LIST_CHANGED);
    }
}

void GpioButtonManager::onButtonTimerChanged() {
    if(_instance == NULL) {
        return;
    }
    log(LOG_INFO, "on btn timer");
    sendEvent(_instance->_inFd, GpioButtonManager::BUTTON_CHANGED);
}

GpioButtonManager::GpioButtonManager() {
    _inFd = eventfd(0, 0);
    if(_inFd == -1) {
        log(LOG_ERR, "unable to call eventfd to GPIO button add/remove: %s",  strerror(errno));
        return;
    }

    _interval.it_value.tv_sec = DEBOUNCE_READ_DELAY / 1000000;
    _interval.it_value.tv_nsec = (DEBOUNCE_READ_DELAY % 1000000)* 1000;
    _interval.it_interval.tv_sec = DEBOUNCE_READ_DELAY / 1000000;
    _interval.it_interval.tv_nsec = (DEBOUNCE_READ_DELAY % 1000000)* 1000;
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
        sendEvent(_inFd, GpioButtonManager::EXIT);
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
    pollfd fdList[66];  //max 64 pins + event + timerfd
    _resetLocalList();
    int fdCount = _initFdList(fdList);

    while(1) {
        if(-1 == poll(fdList, fdCount, -1)) {
           log(LOG_ERR, "unable to listen buttons fd: %s", strerror(errno));
           return;
        }
        if(fdList[0].revents != 0) {
            uint64_t msg = readEvent(_inFd);
            log(LOG_INFO, "button mgnt loop, infd: %llu, %llu, %llu", msg, GpioButtonManager::EXIT, GpioButtonManager::BUTTON_CHANGED);
            if(msg == GpioButtonManager::EXIT){
            log(LOG_INFO, "button mgnt loop, EXIT %llu", msg);
                return;
            }
            else if(msg == GpioButtonManager::BUTTON_LIST_CHANGED) {
                _resetLocalList();
            log(LOG_INFO, "button mgnt loop, BUTTON_LIST_CHANGED %llu", msg);
            }
            else{
                        log(LOG_INFO, "button mgnt loop, BUTTON_CHANGED %llu", msg);
            }
            fdCount = _initFdList(fdList);
        }
        if(fdList[1].revents != 0) { // tick
            clearInfoFd(_timerFd);
            //all debouncing buttons have to read their value
            for(GpioButton* btn : _localBtns) {
                btn->_update();
            }
        }
        for(int i = 2; i < fdCount; i++) {
            log(LOG_INFO, "Button timer check: %d => %d", i, fdList[i].revents);
            if(fdList[i].revents != 0) {
                clearInfoFd(fdList[i].fd);
                log(LOG_INFO, "Button timer catched");
                _localBtns[i-2]->_onDelay();
            }
        }
        log(LOG_INFO, "end loop check");
    }
}


int GpioButtonManager::_initFdList(pollfd *fdList) {
    memset(fdList, 0, sizeof(pollfd)*66);
    int fdCount = 2;

    fdList[0].fd = _inFd;
    fdList[0].events = POLLIN;
    fdList[1].fd = _timerFd;
    fdList[1].events = POLLIN;

    for(GpioButton* btn : _localBtns) {
        if(btn->_timerFd != -1) {
            fdList[fdCount-2].fd = btn->_timerFd;
            fdList[fdCount-2].events = POLLIN;
            ++fdCount;
        }
    }
    log(LOG_INFO, "local list updated %d / %d", fdCount -2, _localBtns.size());
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
