#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <sys/select.h>
#include <algorithm>

#include <bcm2835.h>

#include "config.h"
#include "log.hpp"
#include "led.hpp"
#include "devices.hpp"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1


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

    /* Drop user if there is one, and we were run as root */
    if(( getuid() == 0 || geteuid() == 0) && strcmp(RUN_AS_USER, "root") != 0) {
        struct passwd *pw = getpwnam(RUN_AS_USER);
        if ( pw ) {
            log( LOG_NOTICE, "setting user to " RUN_AS_USER );
            setuid( pw->pw_uid );
        }
    }

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
    if (sid < 0) {
        log( LOG_ERR, "unable to create a new session, code %d (%s)",
                errno, strerror(errno) );
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


bool run() {
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
    if (signalFd == -1) {
         log(LOG_ERR, "signalfd failed!");
         return false;
    }

    Devices devs;
    Led led(RPI_GPIO_P1_23);  //gpio11

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

int main( int argc, char *argv[] ) {
    bool isDaemon = false;
    for(int i = 0; i < argc; i++){
    	if(strcmp(argv[i], "--daemon") == 0){
      	    isDaemon = true;
	    break;
    	}
    }

    bool useSysLog = isDaemon;
    if(!isDaemon){
        for(int i = 0; i < argc; i++){
            if(strcmp(argv[i], "--syslog") == 0){
      	        useSysLog = true;
	        break;
    	    }
        }
    }

    initLog(useSysLog);
    if(getuid() != 0) {
        log(LOG_ERR, "you must be root to run this application");
        return EXIT_FAILURE;
    }
    log(LOG_INFO, "starting");

    if(isDaemon){
        daemonize();
    }

    bool success = run();

    log(LOG_NOTICE, "terminated");
    cleanLog();
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
