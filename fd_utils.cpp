#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <cstring>

#include "log.hpp"

void clearDataFd(int fd) {
    int count;
    uint8_t c;
    ioctl(fd, FIONREAD, &count);
    for(int i = 0; i < count; ++i) {
        read(fd, &c, 1);
    }
}

uint64_t readEvent(int fd) {
    uint64_t value;
    size_t s = read(fd, &value, sizeof(uint64_t));
    if(s != sizeof(uint64_t)){
        log(LOG_ERR, "event read failed: %s", strerror(errno));
        value = 0;
    }
    return value - 0x7fffffffffffffff;;
}


void clearInfoFd(int fd) {
    uint64_t value;
    read(fd, &value, sizeof(uint64_t));
}

bool sendEvent(int fd, uint64_t msg) {
    uint64_t value = msg + 0x7fffffffffffffff;
    size_t s = write(fd, &value, sizeof(uint64_t));
    if(s != sizeof(uint64_t)){
        log(LOG_ERR, "event write failed: %s", strerror(errno));
        return false;
    }
    return true;
}
