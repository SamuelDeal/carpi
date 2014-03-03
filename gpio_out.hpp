#ifndef _GPIO_OUT_HPP_
#define _GPIO_OUT_HPP_

#include "gpio.hpp"

class GpioOut {
    public:
        GpioOut(int pin);
        ~GpioOut();

        void write(Gpio::Value);

    protected:
        int _pin;
};

#endif // _GPIO_OUT_HPP
