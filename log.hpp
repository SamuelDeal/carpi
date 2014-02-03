#include <stdio.h> 
#include <syslog.h>

extern void initLog(bool useSysLog);
extern void cleanLog();
extern bool __useSysLog;

#define log(severity, msg, ...) \
   if(__useSysLog){ \
      syslog(severity, msg, ##__VA_ARGS__); \
   } \
   else{ \
      if(severity <= LOG_ERR){ \
         fprintf(stderr, "Error: " msg "\n", ##__VA_ARGS__); \
      } \
      else{ \
         printf(msg "\n", ##__VA_ARGS__); \
      } \
   }
