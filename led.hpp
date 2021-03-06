#ifndef _LED_HPP
#define _LED_HPP

#include <pthread.h>
#include <mutex>

#include "pipe.hpp"
#include "gpio_out.hpp"


//TODO: perf optim with poll, or better: pwn
//TODO: good error management
class Led {
    public:
       static const char ON = 1;
       static const char OFF = 2;
       static const char BLINK_SLOWLY = 3;
       static const char BLINK_QUICKLY = 4;
       static const char BLINK_NUMBER = 5;

       Led(int ledPin);
       ~Led();
       void on();
       void off();
       void blinkSlowly();
       void blinkQuickly();
       void blinkNumber(unsigned int);

    protected:
       static const char QUIT = 6;
       static const long SLOW_TIME = 300000;
       static const long QUICK_TIME = 50000;
       static const long NUMBER_TIME = 250000;

       Pipe _pipe;
       pthread_t _thread;
       std::mutex _mut;
       bool _isOn;
       char _status;
       GpioOut _pin;
       unsigned int _blinkNumber;
       unsigned int _blinkCount;

       static void* _startBlinking(void*);
       void _light(bool);
       void _stopBlinking();
       bool _blinking();
       void _blink();
       long _getBlinkDelay();
};

#endif // _LED_HPP
