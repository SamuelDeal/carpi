#include <pthread.h>
#include <stdint.h>

class Led {
    public:
       static const uint64_t ON = 1;
       static const uint64_t OFF = 2;
       static const uint64_t BLINK_SLOWLY = 3;
       static const uint64_t BLINK_QUICKLY = 4;

       Led(unsigned short ledPin);
       ~Led();
       void on();
       void off();
       void blinkSlowly();
       void blinkQuickly();

    protected:
       static const uint64_t QUIT = 5;
       static const long SLOW_TIME = 300000;
       static const long QUICK_TIME = 50000;

       unsigned short _pin;
       int _efd;
       pthread_t _thread;
       pthread_mutex_t _mut;
       bool _isOn;
       bool _exit;
       uint64_t _status;

       static void* _startBlinking(void*);
       void _light(bool);
       void _stopBlinking();
       bool _blinking();
       void _blink();
};
