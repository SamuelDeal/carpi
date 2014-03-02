#define DAEMON_NAME "carpi"
#define RUN_AS_USER "pi"
#define PID_FILE "/var/lock/" DAEMON_NAME ".pid"

#define LED_PIN 11

#define IGNORED_PARTITIONS {"boot"}
#define BIG_DISK_NAME "rpi_trip"
#define DOS_PART_OWNER "1000"

#define DEBOUNCE_TIME         30000  //latency, usec
#define DEBOUNCE_READ_DELAY   10000  //read delay, usec

// #define DISABLE_GPIO 1

