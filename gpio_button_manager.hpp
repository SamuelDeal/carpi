#ifndef _GPIO_BUTTON_MANAGER_HPP
#define _GPIO_BUTTON_MANAGER_HPP

#include <pthread.h>
#include <sys/timerfd.h>
#include <mutex>
#include <map>
#include <vector>
#include <poll.h>

class GpioButton;

class GpioButtonManager {
    public:
        static bool add(GpioButton *);
        static void remove(GpioButton *);
        static void onButtonTimerChanged();

    protected:
        static GpioButtonManager* _instance;
        static std::mutex _mut;
        static std::map<int, GpioButton*> _btns;

        static const uint64_t EXIT = 1;
        static const uint64_t BUTTON_CHANGED = 2;
        static const uint64_t BUTTON_LIST_CHANGED = 3;

        GpioButtonManager();
        ~GpioButtonManager();

        static void* _startRun(void* manager);
        void _run();

        int _initFdList(pollfd*);
        void _resetLocalList();
        void _initTimerFd();

        int _inFd;
        int _timerFd;
        itimerspec _interval;
        pthread_t _thread;
        std::vector<GpioButton*> _localBtns;
};

#endif // _GPIO_BUTTON_MANAGER_HPP
