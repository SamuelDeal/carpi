#include "pipe.hpp"

#include <unistd.h>
#include <fcntl.h>

Pipe::Pipe() {
    pipe2(_fds, O_NONBLOCK);
}

Pipe::~Pipe(){
    close(_fds[0]);
    close(_fds[1]);
}

void Pipe::send(char evt) const {
    write(_fds[1], &evt, sizeof(char));
}

char Pipe::read() const {
    char evt = 0;
    ::read(_fds[0], &evt, sizeof(char));
    return evt;
}

int Pipe::getReadFd() const {
    return _fds[0];
}


