#define DAEMON_NAME             "carpi"
#define RUN_AS_USER             "pi"
#define PID_FILE                "/var/lock/" DAEMON_NAME ".pid"

#define LED_PIN                 11

#define IGNORED_PARTITIONS      {"boot"}
#define BIG_DISK_NAME           "rpi_trip"
#define DOS_PART_OWNER          "1000"

// #define DISABLE_GPIO 1

#define DEBOUNCE_TIME           80000  //latency, usec
#define DEBOUNCE_READ_DELAY     10000  //read delay, usec
#define BUTTON_DELAY            800000 // before rebounce or long press, usec
#define BUTTON_MIN_DELAY        100000  // min time before rebounce, usec
#define REBOUNCE_ACCEL          0.7

#define MPD_RECONNECT_DELAY     1000000 // first reconnection delay, usec
#define MPD_RECONNECT_MAXDELAY  30000000 // max reconnection delay, usec
#define MPD_RECONNECT_ACCEL     2

#define PIN_BTN_NEXT            15
#define PIN_BTN_PREV            17
#define PIN_BTN_PAUSE           18

