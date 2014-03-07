#ifndef _PIPE_HPP
#define _PIPE_HPP

class Pipe {
    public:
        Pipe();
        ~Pipe();

        void send(char event) const;
        char read() const;
        int getReadFd() const;

    protected:
        int _fds[2];
};

#endif // _PIPE_HPP

