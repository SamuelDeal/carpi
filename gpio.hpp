#ifndef _GPIO_HPP_
#define _GPIO_HPP_

class Gpio {
    public:
        static bool init();

        enum Value {
            low = 0,
            high = 1
        };

        enum Mode {
            input,
            output
        };

    private: //not implemented, forbidden call
        Gpio();
        Gpio(Gpio const&);
        void operator=(Gpio const&);
};

#endif // _GPIO_HPP
