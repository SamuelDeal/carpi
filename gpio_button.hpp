#ifndef _GPIO_BUTTON_HPP_
#define _GPIO_BUTTON_HPP_

#include <sys/timerfd.h>

#include "gpio.hpp"
#include "pipe.hpp"

class GpioButtonManager;   //internal classes, not usable

class GpioButton {
    public:
        GpioButton(int pin, bool rebounce, bool defaultHight = false);
        ~GpioButton();

        const Pipe& getPipe() const;
        bool isValid() const;

        static const char PRESS = 1;
        static const char RELEASE = 2;
        static const char LONG_PRESS = 3;
        static const char LONG_RELEASE = 4;

    protected:
        friend class GpioButtonManager;

        int _pin;
        Pipe _pipe;
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
