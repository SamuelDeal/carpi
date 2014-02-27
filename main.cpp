#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/select.h>
#include <sys/capability.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>

#include "config.h"
#include "log.hpp"
#include "led.hpp"
#include "devices.hpp"
#include "gpio.hpp"



bool run(bool isDaemon) {
    sigset_t mask;
    struct signalfd_siginfo fdsi;
    ssize_t s;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGTERM);

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
    GpioButton btn1(15);

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

        if(select(std::max(signalFd,devs.getUdevFd())+1, &readFsSet, NULL, NULL, NULL) == -1) {
            log(LOG_ERR, "unable to listen file descriptors: %s", strerror(errno));
            return false;
        }
        if(FD_ISSET(signalFd, &readFsSet)){
            s = read(signalFd, &fdsi, sizeof(struct signalfd_siginfo));
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
    }
    return true;
}

//manage linux capabilities to improve security
bool updateRights() {
    if(!CAP_IS_SUPPORTED(CAP_SYS_ADMIN) || !CAP_IS_SUPPORTED(CAP_SYSLOG)) {
        log(LOG_ERR, "required capabilities are not supported");
        return false;
    }

    cap_t caps = cap_get_proc();
    if(caps == NULL) {
        log(LOG_ERR, "unable to get capabilities: %s", strerror(errno));
        return false;
    }
    cap_value_t capList[] = { CAP_SYS_ADMIN, CAP_SYSLOG/* , CAP_SETUID, CAP_SETGID*/ } ;
    unsigned num_caps = sizeof(capList)/sizeof(cap_value_t);
    if(cap_set_flag(caps, CAP_EFFECTIVE, num_caps, capList, CAP_SET) ||
        cap_set_flag(caps, CAP_INHERITABLE, num_caps, capList, CAP_SET) ||
        cap_set_flag(caps, CAP_PERMITTED, num_caps, capList, CAP_SET)) {
            log(LOG_ERR, "unable to set capability flags: %s", strerror(errno));
            return false;
    }

    if(cap_set_proc(caps) == -1) {
        log(LOG_ERR, "unable to set capabilities: %s", strerror(errno));
        return false;
    }

    if (prctl(PR_SET_KEEPCAPS, 1L)) {
        log(LOG_ERR, "prctl failed %s", strerror(errno));
        return false;
    }

    // Drop user if there is one, and we were run as root
    if(( getuid() == 0 || geteuid() == 0) && strcmp(RUN_AS_USER, "root") != 0) {
        struct passwd *pw = getpwnam(RUN_AS_USER);
        if(!pw) {
            log(LOG_ERR, "unknow user %s", RUN_AS_USER);
            return false;
        }
        if(setuid(pw->pw_uid)) {
            log(LOG_ERR, "unable to set user %s", strerror(errno));
            return false;
        }
        log(LOG_NOTICE, "setting user to " RUN_AS_USER);
    }

    if(cap_set_proc(caps) == -1) {
        log(LOG_ERR, "unable to set capabilities: %s", strerror(errno));
        return false;
    }

    if(cap_free(caps) == -1) {
        log(LOG_ERR, "unable to free capabilities: %s", strerror(errno));
        return false;
    }
}

static void child_handler(int signum){
    switch(signum) {
    	case SIGUSR1:
		exit(EXIT_SUCCESS);
		break;

  	case SIGALRM:
    	case SIGCHLD:
	default:
		exit(EXIT_FAILURE);
		break;
    }
}

static void daemonize(){
    pid_t pid;
    pid_t sid;
    pid_t parent;
    int lfp = -1;

    if(getppid() == 1){
	return; // already a daemon
    }

    //Create the lock file as the current user
    lfp = open(PID_FILE,O_RDWR|O_CREAT,0640);
    if ( lfp < 0 ) {
        log( LOG_ERR, "unable to create lock file %s, code=%d (%s)",
                PID_FILE, errno, strerror(errno) );
        exit(EXIT_FAILURE);
    }

    updateRights();

    /* Trap signals that we expect to recieve */
    signal(SIGCHLD,child_handler);
    signal(SIGUSR1,child_handler);
    signal(SIGALRM,child_handler);

    /* Fork off the parent process */
    pid = fork();
    if (pid < 0) {
        log( LOG_ERR, "unable to fork daemon, code=%d (%s)",
                errno, strerror(errno) );
        exit(EXIT_FAILURE);
    }
    /* If we got a good PID, then we can exit the parent process. */
    if (pid > 0) {

        /* Wait for confirmation from the child via SIGTERM or SIGCHLD, or
           for two seconds to elapse (SIGALRM).  pause() should not return. */
        alarm(2);
        pause();

        exit(EXIT_FAILURE);
    }

    /* At this point we are executing as the child process */
    parent = getppid();

    /* Cancel certain signals */
    signal(SIGCHLD,SIG_DFL); /* A child process dies */
    signal(SIGTSTP,SIG_IGN); /* Various TTY signals */
    signal(SIGTTOU,SIG_IGN);
    signal(SIGTTIN,SIG_IGN);
    signal(SIGHUP, SIG_IGN); /* Ignore hangup signal */

    /* Change the file mode mask */
    umask(0);

    /* Create a new SID for the child process */
    sid = setsid();
    if(sid < 0) {
        log(LOG_ERR, "unable to create a new session, code %d (%s)", errno, strerror(errno) );
        exit(EXIT_FAILURE);
    }

    /* Change the current working directory.  This prevents the current
       directory from being locked; hence not being able to remove it. */
    if ((chdir("/")) < 0) {
        syslog( LOG_ERR, "unable to change directory to %s, code %d (%s)",
                "/", errno, strerror(errno) );
        exit(EXIT_FAILURE);
    }

    /* Redirect standard files to /dev/null */
    freopen( "/dev/null", "r", stdin);
    freopen( "/dev/null", "w", stdout);
    freopen( "/dev/null", "w", stderr);

    /* Tell the parent process that we are A-okay */
    kill( parent, SIGUSR1 );
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
