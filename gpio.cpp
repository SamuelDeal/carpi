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
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <map>
#include <list>

#include "log.hpp"
#include "config.h"
#include "gpio_core.hpp"


bool Gpio::init() {
    return !GpioCore::get()._initFailed;
}


GpioOut::GpioOut(int pin) {
    _pin = pin;
    GpioCore::get().setPinMode(pin, Gpio::output);
}

GpioOut::~GpioOut() {
}

void GpioOut::write(Gpio::Value value) {
    GpioCore::get().writePin(_pin, value);
}


