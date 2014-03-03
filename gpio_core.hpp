#ifndef _GPIO_CORE_HPP
#define _GPIO_CORE_HPP

#include <stdint.h>

#include "gpio.hpp"

//TODO: manage errors
//TODO: check if it's running on a raspberry pi
//TODO: allow disabling by config.h macro
//TODO: add credits
//TODO: add pwn for led

// this class should not be used directly
class GpioCore {
    protected:
        friend class GpioButton;
        friend class GpioOut;
        friend class Gpio;

        enum PullStatus {
            pullUp = 2,
            pullDown = 1,
            pullOff = 0
        };

        static GpioCore& get() {
            static GpioCore instance;
            return instance;
        };
        void writePin(int pin, Gpio::Value);
        void setPinMode(int pin, Gpio::Mode);
        Gpio::Value readPin(int pin);
        void setPull(int pin, GpioCore::PullStatus);

    protected:
        bool _initFailed;
        int *_pins;
        volatile uint32_t *_gpioMem;
        int getBoardRev() const;
        ~GpioCore();

    private:
        GpioCore();
        GpioCore(Gpio const&); //not implemented, forbidden call
        void operator=(GpioCore const&); //not implemented, forbidden call
};

#endif // _GPIO_CORE_HPP
