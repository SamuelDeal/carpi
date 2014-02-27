#ifndef _GPIO_HPP_
#define _GPIO_HPP_

#include <stdint.h>

class GpioOut;
class GpioButton;

class Gpio {
    public:
        static bool init();
        ~Gpio();

        enum Value {
            low = 0,
            high = 1
        };

        enum Mode {
            input,
            output
        };

    protected:
        friend class GpioButton;
        friend class GpioOut;

        static Gpio& get() {
            static Gpio instance;
            return instance;
        };
        void writePin(int pin, Gpio::Value);
        void setPinMode(int pin, Gpio::Mode);


    protected:
        bool _initFailed;
        int *_pins;
        volatile uint32_t *_gpioMem;
        int getBoardRev() const;

    private:
        Gpio();
        Gpio(Gpio const&); //not implemented, forbidden call
        void operator=(Gpio const&); //not implemented, forbidden call
};

class GpioOut {
    public:
        GpioOut(int pin);
        ~GpioOut();

        void write(Gpio::Value);

    protected:
        int _pin;
};

class GpioButton {
    public:
        GpioButton(int pin);
        ~GpioButton();

        int getEventFd() const;
        bool isValid() const;

        static const uint64_t CLICK = 1;
        static const uint64_t LONG_PUSH = 2;
        static const uint64_t LONG_RELEASE = 3;

    protected:
        int _pin;
        int _efd;
        int _sysFd;
        bool _exported;
        bool _initFailed;
};

#endif // _GPIO_HPP
