#define DAEMON_NAME "carpi"
#define RUN_AS_USER "pi"
#define PID_FILE "/var/lock/" DAEMON_NAME ".pid"

#define RPI_REV 2
#define LED_PIN 11

#define IGNORED_PARTITIONS {"boot"}
#define BIG_DISK_NAME "rpi_trip"
#define DOS_PART_OWNER "1000"

// #define DISABLE_GPIO 1

#define MY_LED_11 "coucou"
#define LED_FINDER(pin) MY_LED_ ## pin
#define GOOD_LED LED_FINDER(LED_PIN)
