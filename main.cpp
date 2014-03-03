#include <cstring>
#include <signal.h>
#include <sys/signalfd.h>

#include "config.h"
#include "process.hpp"
#include "log.hpp"
#include "led.hpp"
#include "devices.hpp"
#include "gpio.hpp"
#include "gpio_out.hpp"
#include "gpio_button.hpp"
#include "fd_utils.hpp"

//TODO: better error management
//TODO: handle sigterm with sigaction, off the led and mount drive in r/o mode
bool run(bool isDaemon) {
    sigset_t mask;
    struct signalfd_siginfo fdsi;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);

    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
         log(LOG_ERR, "sigprocmask failed!");
         return false;
    }

    int signalFd = signalfd(-1, &mask, 0);
    if(signalFd == -1) {
        log(LOG_ERR, "signalfd failed!");
        return false;
    }

    if(!Gpio::init()) {
        log(LOG_ERR, "gpio initialisation failed!");
        return false;
    }

    Devices devs;
    Led led(LED_PIN);
    GpioButton btn1(23, true);
    if(!btn1.isValid()) {
        log(LOG_ERR, "btn init failed");
        return false;
    }

    if(!devs.isBigDiskConnected()) {
        led.blinkQuickly();
    }
    else if(devs.isCopyAvailable()) {
        led.blinkSlowly();
    }
    else {
        led.on();
    }

    bool exit = false;
    if(!isDaemon) {
        log(LOG_INFO, "press Ctrl-C to quit");
    }
    while(!exit) {
        fd_set readFsSet;
        FD_ZERO(&readFsSet);
        FD_SET(signalFd, &readFsSet);
        FD_SET(devs.getUdevFd(), &readFsSet);
        FD_SET(btn1.getEventFd(), &readFsSet);

        int max = std::max(signalFd,devs.getUdevFd());
        max = std::max(max, btn1.getEventFd());

        if(select(max+1, &readFsSet, NULL, NULL, NULL) == -1) {
            log(LOG_ERR, "unable to listen file descriptors: %s", strerror(errno));
            return false;
        }
        if(FD_ISSET(signalFd, &readFsSet)){
            read(signalFd, &fdsi, sizeof(struct signalfd_siginfo));
            if((fdsi.ssi_signo == SIGINT) && !isDaemon) {
                printf("\n");
            }
            log(LOG_INFO, "signal catched, exiting");
            exit = true;
        }
        else if(FD_ISSET(devs.getUdevFd(), &readFsSet)) {
            devs.manageChanges();
            if(!devs.isBigDiskConnected()) {
                led.blinkQuickly();
            }
            else if(devs.isCopyAvailable()) {
                led.blinkSlowly();
            }
            else {
                led.on();
            }
        }
        else if(FD_ISSET(btn1.getEventFd(), &readFsSet)){
            uint64_t msg = readEvent(btn1.getEventFd());
            log(LOG_INFO, "btn event %llu", msg);
        }
    }
    return true;
}

int main( int argc, char *argv[] ) {
    bool isDaemon = false;
    for(int i = 0; i < argc; i++) {
    	if(strcmp(argv[i], "--daemon") == 0) {
      	    isDaemon = true;
	    break;
    	}
    }

    bool useSysLog = isDaemon;
    if(!isDaemon) {
        for(int i = 0; i < argc; i++) {
            if(strcmp(argv[i], "--syslog") == 0) {
      	        useSysLog = true;
	        break;
    	    }
        }
    }

    initLog(useSysLog);
    if(getuid() != 0) { //you are not root
        if(!isDaemon) { //syslog may not write in good place, use std instead
            fprintf(stderr, "you must be root to run this application\n");
        }
        return EXIT_FAILURE;
    }
    log(LOG_INFO, "starting");

    if(isDaemon) {
        daemonize();
    }
    else {
        updateRights();
    }

    bool success = run(isDaemon);

    log(LOG_NOTICE, "terminated");
    cleanLog();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
