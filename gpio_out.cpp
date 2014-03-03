#include "gpio_out.hpp"

#include "gpio_core.hpp"

GpioOut::GpioOut(int pin) {
    _pin = pin;
    GpioCore::get().setPinMode(pin, Gpio::output);
}

GpioOut::~GpioOut() {
}

void GpioOut::write(Gpio::Value value) {
    GpioCore::get().writePin(_pin, value);
}


