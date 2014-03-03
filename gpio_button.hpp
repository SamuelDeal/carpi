#ifndef _GPIO_BUTTON_HPP_
#define _GPIO_BUTTON_HPP_

#include <stdint.h>
#include <sys/timerfd.h>

#include "gpio.hpp"

class GpioButtonManager;   //internal classes, not usable

class GpioButton {
    public:
        GpioButton(int pin, bool rebounce, bool defaultHight = false);
        ~GpioButton();

        int getEventFd() const;
        bool isValid() const;

        static const uint64_t PRESS = 1;
        static const uint64_t RELEASE = 2;
        static const uint64_t LONG_PRESS = 3;
        static const uint64_t LONG_RELEASE = 4;

    protected:
        friend class GpioButtonManager;

        int _pin;
        int _eventFd;
        int _timerFd;
        bool _initFailed;
        Gpio::Value _status;
        bool _defaultHigh;
        int _integrator;
        bool _rebounce;
        bool _long; // is thenbutton pressed for long time ?
        itimerspec _interval;

        Gpio::Value _integrate(Gpio::Value input);
        void _update();
        void _onDelay();
};

#endif // _GPIO_BUTTON_HPP
