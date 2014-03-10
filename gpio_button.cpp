#include "gpio_button.hpp"

#include <cstring>

#include "config.h"
#include "log.hpp"
#include "gpio_core.hpp"
#include "gpio_button_manager.hpp"

#define INTEGRATOR_MAXIMUM    (DEBOUNCE_TIME / DEBOUNCE_READ_DELAY)

const char GpioButton::PRESS;
const char GpioButton::RELEASE;
const char GpioButton::LONG_PRESS;
const char GpioButton::LONG_RELEASE;

GpioButton::GpioButton(int pin, bool rebounce, bool defaultHigh) {
    _pin = pin;
    _initFailed = true;
    GpioCore::get().setPinMode(pin, Gpio::input);
    GpioCore::get().setPull(pin, GpioCore::pullOff);
    _status = defaultHigh ? Gpio::high : Gpio::low;
    _integrator = defaultHigh ? INTEGRATOR_MAXIMUM : 0;
    _defaultHigh = defaultHigh;
    _rebounce = rebounce;
    _timerFd = -1;
    _long = false;

    _initFailed = !GpioButtonManager::add(this);
}

GpioButton::~GpioButton() {
    if(!_initFailed) {
        GpioButtonManager::remove(this);
    }
    if(_timerFd != -1) {
        close(_timerFd);
    }
}

bool GpioButton::isValid() const {
    return !_initFailed;
}

const Pipe& GpioButton::getPipe() const {
    return _pipe;
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
    if(output == _status) {
        return;
    }
    _status = output;
    if(((_status == Gpio::high) && !_defaultHigh) || ((_status == Gpio::low) && _defaultHigh)) {
        _pipe.send(GpioButton::PRESS);

        _interval.it_value.tv_sec = BUTTON_DELAY / 1000000;
        _interval.it_value.tv_nsec = (BUTTON_DELAY % 1000000) * 1000;
        long next = _rebounce ? computeNextDelay(BUTTON_DELAY * 1000) : (BUTTON_DELAY * 1000);
        _interval.it_interval.tv_sec = next / 1000000000;
        _interval.it_interval.tv_nsec = next % 1000000000;

        if(_timerFd == -1) {
            _timerFd = timerfd_create(CLOCK_MONOTONIC, 0);
        }
        timerfd_settime(_timerFd, 0, &_interval, NULL);
        if(_timerFd == -1) {
            log(LOG_ERR, "unable to create timer for debouncing: %s",  strerror(errno));
        }
    }
    else {
        _pipe.send(_long ? GpioButton::LONG_RELEASE : GpioButton::RELEASE);
        if(_timerFd != -1) {
            close(_timerFd);
            _timerFd = -1;
        }
    }
    GpioButtonManager::onButtonTimerChanged();
    _long = false;
}

void GpioButton::_onDelay() {
     _pipe.send(_rebounce ? GpioButton::PRESS : GpioButton::LONG_PRESS);

     if(_rebounce) {
         long next = computeNextDelay(_interval.it_value.tv_nsec);
         _interval.it_value.tv_sec = next / 1000000000;
         _interval.it_value.tv_nsec = next % 1000000000;
         next = computeNextDelay(next);
         _interval.it_interval.tv_sec = next / 1000000000;
         _interval.it_interval.tv_nsec = next % 1000000000;
         timerfd_settime(_timerFd, 0, &_interval, NULL);
     }
     else if(_timerFd != -1) {
         GpioButtonManager::onButtonTimerChanged();
         close(_timerFd);
         _timerFd = -1;
     }
     _long = true;
}


