#ifndef _FS_UTILS_HPP
#define _FS_UTILS_HPP

#include <stdint.h>

//Clear any initial pending interrupt
void clearReads(int fd);

//get message from fd
uint64_t readEvent(int fd);

//send message to fd
bool sendEvent(int fd, uint64_t msg);

#endif // _FS_UTILS_HPP
