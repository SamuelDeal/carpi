#ifndef _FS_UTILS_HPP
#define _FS_UTILS_HPP

// #include <stdint.h>

//Clear any message on timerfd or eventfd
void clearInfoFd(int fd);

//Clear any pending data on file or socketmessage
void clearDataFd(int fd);

//get message from fd
uint64_t readEvent(int fd);

//send message to fd
bool sendEvent(int fd, uint64_t msg);

#endif // _FS_UTILS_HPP
