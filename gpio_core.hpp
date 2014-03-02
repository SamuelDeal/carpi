#ifndef _GPIO_CORE_HPP
#define _GPIO_CORE_HPP

#include "gpio.hpp"

// this class
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
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <map>
#include <list>

#include "log.hpp"
#include "config.h"

// this class should not be used directly
class GpioCore {
    protected:
        friend class GpioButton;
        friend class GpioOut;
        friend class Gpio;

        static GpioCore& get() {
            static GpioCore instance;
            return instance;
        };
        void writePin(int pin, Gpio::Value);
        void setPinMode(int pin, Gpio::Mode);
        Gpio::Value readPin(int pin);
        bool exportPin(int pin);
        bool unexportPin(int pin);
        int getPinFd(int pin);

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
