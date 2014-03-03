#include "gpio.hpp"

#include "gpio_core.hpp"


bool Gpio::init() {
    return !GpioCore::get()._initFailed;
}



