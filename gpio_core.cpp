#include "gpio_core.hpp"

#include <stdio.h>
#include <ctype.h>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "config.h"

#define	BLOCK_SIZE		(4*1024)
#define	GPPUD	37
#define BCM2708_PERI_BASE 0x20000000
#define GPIO_BASE   (BCM2708_PERI_BASE + 0x00200000)

static int pinToGpioR1 [64] = {
  17, 18, 21, 22, 23, 24, 25, 4,	// From the Original Wiki - GPIO 0 through 7
   0, 1, // I2C - SDA0, SCL0
   8, 7, // SPI - CE1, CE0
  10, 9, 11, // SPI - MOSI, MISO, SCLK
  14, 15, // UART - Tx, Rx

  // Padding:
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 31
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 47
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 63
};

static int pinToGpioR2 [64] = {
  17, 18, 27, 22, 23, 24, 25, 4, // From the Original Wiki - GPIO 0 through 7: wpi 0 - 7
   2, 3, // I2C - SDA0, SCL0 wpi 8 - 9
   8, 7, // SPI - CE1, CE0 wpi 10 - 11
  10, 9, 11, // SPI - MOSI, MISO, SCLK wpi 12 - 14
  14, 15, // UART - Tx, Rx wpi 15 - 16
  28, 29, 30, 31, // New GPIOs 8 though 11 wpi 17 - 20

  // Padding:
                      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 31
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 47
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 63
};

//(Word) offset to the GPIO Set registers for each GPIO pin
static uint8_t gpioToGPSET [] = {
   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
   8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
};

//(Word) offset to the GPIO Clear registers for each GPIO pin
static uint8_t gpioToGPCLR [] = {
  10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
  11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,
};

//Map a BCM_GPIO pin to it's control port. (GPFSEL 0-5)
static uint8_t gpioToGPFSEL [] = {
  0,0,0,0,0,0,0,0,0,0,
  1,1,1,1,1,1,1,1,1,1,
  2,2,2,2,2,2,2,2,2,2,
  3,3,3,3,3,3,3,3,3,3,
  4,4,4,4,4,4,4,4,4,4,
  5,5,5,5,5,5,5,5,5,5,
};

//Define the shift up for the 3 bits per pin in each GPFSEL port
static uint8_t gpioToShift [] = {
  0,3,6,9,12,15,18,21,24,27,
  0,3,6,9,12,15,18,21,24,27,
  0,3,6,9,12,15,18,21,24,27,
  0,3,6,9,12,15,18,21,24,27,
  0,3,6,9,12,15,18,21,24,27,
};


//(Word) offset to the GPIO Input level registers for each GPIO pin
static uint8_t gpioToGPLEV [] = {
  13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
  14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
};

//(Word) offset to the Pull Up Down Clock regsiter
static uint8_t gpioToPUDCLK [] = {
  38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,
  39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,
};



GpioCore::GpioCore() {
    _initFailed = true;
    int revId = getBoardRev();
    if(revId == -1) {
       return;
    }

    int fd ;
    if((fd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) {
       return;
    }
    _gpioMem = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GPIO_BASE) ;
    if((int32_t)_gpioMem == -1) {
       return;
    }
    _pins = revId == 1 ? pinToGpioR1 : pinToGpioR2;
    _initFailed = false;
}

GpioCore::~GpioCore() {
}


int GpioCore::getBoardRev() const {
    FILE *cpuFd;
    char line[150];
    char *c, lastChar;

    if((cpuFd = fopen("/proc/cpuinfo", "r")) == NULL) {
        return -1;
    }
    while(fgets(line, 120, cpuFd) != NULL) {
        if(strncmp(line, "Revision", 8) == 0) {
            break;
        }
    }
    fclose(cpuFd);
    if(strncmp(line, "Revision", 8) != 0) {
        return -1;
    }
    for(c = &line[strlen(line) - 1]; (*c == '\n') || (*c == '\r') ; --c) {
        *c = 0 ;
    }
    for(c = line; *c; ++c) {
        if(isdigit(*c)) {
            break;
        }
    }
    if(!isdigit(*c)) {
        return -1;
    }
    lastChar = line[strlen(line) - 1];
    return ((lastChar == '2') || (lastChar == '3')) ? 1 : 2;
}

void GpioCore::writePin(int pin, Gpio::Value value){
    pin &= 63 ;

    if(value == Gpio::high) {
        *(_gpioMem + gpioToGPSET[pin]) = 1 << (pin & 31);
    }
    else {
        *(_gpioMem + gpioToGPCLR[pin]) = 1 << (pin & 31);
    }
}



Gpio::Value GpioCore::readPin(int pin) {
    pin &= 63 ;
    if((*(_gpioMem + gpioToGPLEV[pin]) & (1 << (pin & 31))) != 0) {
        return Gpio::high;
    }
    else {
        return Gpio::low;
    }
}


void GpioCore::setPinMode(int pin, Gpio::Mode mode) {
    //  register int barrier ;
    int fSel, shift;

    pin &= 63 ;

    fSel    = gpioToGPFSEL[pin] ;
    shift   = gpioToShift[pin] ;

    if(mode == Gpio::input) {
        *(_gpioMem + fSel) = (*(_gpioMem + fSel) & ~(7 << shift)) ; // Sets bits to zero = input
    }
    else { // mode == Gpio::output
        *(_gpioMem + fSel) = (*(_gpioMem + fSel) & ~(7 << shift)) | (1 << shift) ;
    }
}


void delayMicroseconds (unsigned int howLong) {
  struct timeval tNow, tLong, tEnd ; 
  gettimeofday (&tNow, NULL) ; 
  tLong.tv_sec = howLong / 1000000 ; 
  tLong.tv_usec = howLong % 1000000 ; 
  timeradd (&tNow, &tLong, &tEnd) ; 
  while (timercmp (&tNow, &tEnd, <))
    gettimeofday (&tNow, NULL) ;
}


void GpioCore::setPull(int pin, GpioCore::PullStatus pull) {
  int pud = (int)pull;
  pin &= 63 ;
  pud &=  3 ;

  *(_gpioMem + GPPUD) = pud;
  delayMicroseconds (5) ;
  *(_gpioMem + gpioToPUDCLK [pin]) = 1 << (pin & 31);
  delayMicroseconds (5) ;

  *(_gpioMem + GPPUD) = 0 ;
  delayMicroseconds (5) ;
  *(_gpioMem + gpioToPUDCLK [pin]) = 0;
  delayMicroseconds (5) ;
}
