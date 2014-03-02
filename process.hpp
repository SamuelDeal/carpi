#ifndef _PROCESS_HPP
#define _PROCESS_HPP


//manage linux capabilities to improve security
bool updateRights();

//demonize (fork, pid file, console streams)
void daemonize();


#endif // _PROCESS_HPP
