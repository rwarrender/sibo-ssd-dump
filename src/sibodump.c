// TODO: Device selection
// TODO: MD5 error checking
// TODO: Timeout for reads and counting of bytes

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
// #include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include "statwrap.h"
#include <sys/time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <tchar.h>
    const char *slash = "\\";

    #define B9600   CBR_9600
    #define B57600  CBR_57600
    #define B115200  CBR_115200
#else
    // Assume it's something POSIX-compliant
    #include <unistd.h>
    #include <termios.h>

    #include <sys/ioctl.h>
    #include <IOKit/serial/ioss.h>
    const char *slash = "/";
#endif

#define BAUDRATE B115200

#define PORT_COMMAND_RESET_SSD      'R'
#define PORT_COMMAND_FETCH_BLOCK    'f'
#define PORT_COMMAND_NEXT_DEVICE    'N'
#define PORT_COMMAND_NEXT_BLOCK     'n'
#define PORT_COMMAND_GET_SSDINFO    'b'
#define PORT_COMMAND_GET_ASIC_TYPE  'a' 

#include "argparse/argparse.h"
static const char *const usage[] = {
    "sibodump [options] [[--] args]",
    "sibodump [options]",
    NULL,
};

struct {
    unsigned char infobyte;
    unsigned char type;
    unsigned char devs;
    unsigned char size;
    unsigned int  blocks;
    unsigned char asic;
} ssdinfo;

#ifdef _WIN32
typedef struct {
    HANDLE portHandle;
    const char *device;
} SerialDevice;
#else
typedef struct {
    int fd;
    const char *device;
    struct termios tty;
} SerialDevice;
#endif

#ifdef _WIN32
void usleep(int us) {
    Sleep(us/1000);
}
#endif


//
// Serial Port Configuration
//
#ifdef _WIN32

void PrintCommState(DCB dcb) {
    //  Print some of the DCB structure values
    _tprintf( TEXT("BaudRate = %ld, ByteSize = %d, Parity = %d, StopBits = %d\n"), 
              dcb.BaudRate, 
              dcb.ByteSize, 
              dcb.Parity,
              dcb.StopBits );
}

int portopen(SerialDevice *sd) {
    sd->portHandle = CreateFile(sd->device, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (sd->portHandle == INVALID_HANDLE_VALUE) {
        fputs("Unable to open serial port", stderr);
        // printf("Error opening serial port %s\n", sd->device);
        sd->portHandle = NULL;
        return 0;
    }
    // printf("%lld\n", sd->portHandle);
    return 0;
}

int portcfg(SerialDevice *sd, int speed) {
    BOOL Success;
    DCB dcb;
    COMMTIMEOUTS timeouts = { 0 };

    dcb.DCBlength = sizeof(dcb);
    Success = GetCommState(sd->portHandle, &dcb);
    if (!Success) {
        printf("GetCommState failed with error %ld.\n", GetLastError());
        return 2;
    }
    // PrintCommState(dcb);

    dcb.BaudRate = speed; // TODO: Test alternative methods
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    
    timeouts.ReadIntervalTimeout         = 50; // in milliseconds
    timeouts.ReadTotalTimeoutConstant    = 50; // in milliseconds
    timeouts.ReadTotalTimeoutMultiplier  = 10; // in milliseconds
    timeouts.WriteTotalTimeoutConstant   = 50; // in milliseconds
    timeouts.WriteTotalTimeoutMultiplier = 10; // in milliseconds

    // PrintCommState(dcb);

    Success = SetCommState(sd->portHandle, &dcb);
    if (!Success) {
        printf("SetCommState failed with error %ld.\n", GetLastError());
        return 1;
    }
    Success = GetCommState(sd->portHandle, &dcb);
    if (!Success) {
        printf("Second GetCommState failed with error %ld.\n", GetLastError());
        return 3;
    }
    // PrintCommState(dcb);


    Success = SetCommTimeouts(sd->portHandle, &timeouts);
    return 0;
}

char* portname(SerialDevice *sd) {
    return sd->device;
}

int portsend(SerialDevice *sd, char buffer) {
    DWORD count;
    // printf("Trying to send '%s' (%x)...\n", &buffer, buffer);
    WriteFile(sd->portHandle, &buffer, 1, &count, NULL);
    return (int) count;
}

int portread(SerialDevice *sd, unsigned char *buffer) {
    DWORD count;
    ReadFile(sd->portHandle, buffer, 1, &count, NULL);
    return (int) count;
}

int portflush(SerialDevice *sd) {
    return PurgeComm(sd->portHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);
}

int portclose(SerialDevice *sd) {
    return CloseHandle(sd->portHandle);
}

unsigned long microsecond_time() {
    // Perhaps this code will work for windows?
    // https://git.postgresql.org/gitweb/?p=postgresql.git;a=blob;f=src/port/gettimeofday.c;h=75a91993b74414c0a1c13a2a09ce739cb8aa8a08;hb=HEAD
    return 0;
}

#else

int portopen(SerialDevice *sd) {
    sd->fd = open(sd->device, O_RDWR | O_NOCTTY);
    if (sd->fd < 0) {
        perror(sd->device);
        exit(-1);
    }
    return 0;
}

int portcfg(SerialDevice *sd, int speed) {
    struct termios tty;

    if (tcgetattr(sd->fd, &tty) < 0) {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        return -1;
    }
    
    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);

    tty.c_cflag |= (CLOCAL | CREAD);    /* ignore modem controls */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         /* 8-bit characters */
    tty.c_cflag &= ~PARENB;     /* no parity bit */
    tty.c_cflag &= ~CSTOPB;     /* only need 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;    /* no hardware flowcontrol */

    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(sd->fd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }

    #ifdef __APPLE__
    // Boost to non-standard speed on Mac OS
    unsigned int speeda = 2000000;
    ioctl(sd->fd, IOSSIOSPEED, &speeda);
    #endif

    return 0;
}

const char* portname(SerialDevice *sd) {
    return sd->device;
}

int portsend(SerialDevice *sd, char buffer) {
    return write(sd->fd, &buffer, 1);
}

static inline int portread(SerialDevice *sd, unsigned char *buffer);
extern inline int portread(SerialDevice *sd, unsigned char *buffer) {
    return read(sd->fd, buffer, 1);
}

int portflush(SerialDevice *sd) {
    return tcflush(sd->fd, TCIFLUSH);
}

int portclose(SerialDevice *sd) {
    return close(sd->fd);
}

unsigned long microsecond_time() {
    struct timeval time;
    gettimeofday(&time, NULL);
    return (time.tv_sec * 1000) + (time.tv_usec / 1000);
}

#endif


void GetSSDInfo(char input) {
    ssdinfo.infobyte = input;

    ssdinfo.type   = (ssdinfo.infobyte & 0b11100000) >> 5;
    ssdinfo.devs   = ((ssdinfo.infobyte & 0b00011000) >> 3) + 1;
    ssdinfo.size   = (ssdinfo.infobyte & 0b00000111);
    ssdinfo.blocks = (ssdinfo.size == 0) ? 0 : ((0b10000 << ssdinfo.size) * 4);
}

void printinfo() {
    printf("ASIC: %d\n", ssdinfo.asic);
    if (ssdinfo.infobyte == 0) {
        printf("No SSD detected.\n");
        return;
    }

    printf("TYPE: ");
    switch (ssdinfo.type) {
        case 0:
            printf("RAM");
            break;
        case 1:
            printf("Type 1 Flash");
            break;
        case 2:
            printf("Type 2 Flash");
            break;
        case 3:
            printf("TBS");
            break;
        case 4:
            printf("TBS");
            break;
        case 5:
            printf("???");
            break;
        case 6:
            printf("ROM");
            break;
        case 7:
            printf("Hardware write-protected SSD");
            break;
    }
    printf("\n");

    printf("DEVICES: %d\n", ssdinfo.devs);

    printf("SIZE: ");
    switch (ssdinfo.size) {
        case 0:
            printf("No SSD Detected");
            break;
        case 1:
            printf("32KB");
            break;
        case 2:
            printf("64KB");
            break;
        case 3:
            printf("128KB");
            break;
        case 4:
            printf("256KB");
            break;
        case 5:
            printf("512KB");
            break;
        case 6:
            printf("1MB");
            break;
        case 7:
            printf("2MB");
            break;
    }
    printf("\n");

    printf("BLOCKS: %d\n", ssdinfo.blocks);
}

void dump(SerialDevice *sd, const char *path) {
    FILE *fp;
    unsigned char buffer;
    unsigned int i;

    printf("\nDumping to %s\n", path);

    fp = fopen(path, "wb");

    portsend(sd, 'd');
    for (i = 1; i <= ssdinfo.blocks * ssdinfo.devs * 256; i++) {
        portread(sd, &buffer);
        fwrite(&buffer, sizeof(buffer), 1, fp);
    }

    fclose(fp);
    return;
}

int portreadfill(SerialDevice *sd, unsigned char *buffer, unsigned int buffer_len) {
    int remain = buffer_len;
    int filled = 0;
    int count = 0;

    while ((count = read(sd->fd, buffer + filled, buffer_len - filled))) {
        // Update counters
        remain -= count;
        filled += count;

        if (remain <= 0) {
            continue;
        }
    }

    // Handle error
    if (count < 0) {
        return count;
    }

    return filled;
}

void getblock(SerialDevice *sd, unsigned int blocknum, unsigned char curdev, unsigned char *block) {
    printf("Fetch block %d (0 to %d, total %d) on device %d\r", blocknum, ssdinfo.blocks - 1, ssdinfo.blocks, curdev);
    fflush(stdout);

    portsend(sd, PORT_COMMAND_FETCH_BLOCK);
    portreadfill(sd, &block[0], 256);
}


int main (int argc, const char **argv) {
    SerialDevice sd;
    int i;
    const char *dumppath = NULL;
    int result;
    unsigned char buffer;
    unsigned int curblock = 0;
    unsigned char block[256];
    unsigned char curdev;
    int firstblockonly = 0;
    int allow_asic4 = 0;
    FILE *fp;
    int wlen;
    unsigned int address;

    struct argparse_option options[] = {
        OPT_HELP(),
        OPT_STRING('s', "serial", &sd.device, "set serial device of arduino"),
        OPT_STRING('d', "dump", &dumppath, "dump to file"),
        OPT_BOOLEAN('f', "firstblockonly", &firstblockonly, "only pull the first block (256 characters)"),
        OPT_BOOLEAN('4', "asic4", &allow_asic4, "Allow native ASIC4 mode for compatible SSDs (EXPERIMENTAL)"),
        // OPT_INTEGER('a', "address", &address, "Start address."),
        OPT_END(),
    };
    struct argparse argparse;
    argparse_init(&argparse, options, usage, 0);
    argparse_describe(&argparse, "\nDumps SIBO SSD images to file.", "");
    argc = argparse_parse(&argparse, argc, argv);

    portopen(&sd);
    portcfg(&sd, B115200);
    usleep(2000000);
    portflush(&sd);
    printf("DEVICE: %s\n", portname(&sd));

    if (allow_asic4 == 0) {
        printf("FORCING ASIC5!\n");

        wlen = portsend(&sd, '5');
    } else {
        printf("ALLOWING ASIC4!\n");
        wlen = portsend(&sd, '4');
    }
    if (wlen != 1) {
        printf("Error from write: %d, %d\n", wlen, errno);
    }

    wlen = portsend(&sd, PORT_COMMAND_GET_SSDINFO);
    if (wlen != 1) {
        printf("Error from write: %d, %d\n", wlen, errno);
    }

    wlen = portread(&sd, &buffer);
    if (wlen != 1) {
        printf("Error from read: %d, %d\n", wlen, errno);
    }
    GetSSDInfo(buffer);

    wlen = portsend(&sd, PORT_COMMAND_GET_ASIC_TYPE);
    if (wlen != 1) {
        printf("Error from write: %d, %d\n", wlen, errno);
    }

    wlen = portread(&sd, &buffer);
    if (wlen != 1) {
        printf("Error from read: %d, %d\n", wlen, errno);
    }
    ssdinfo.asic = buffer;
    printinfo();

    if (dumppath != NULL) {
        printf("\n");
        portsend(&sd, PORT_COMMAND_RESET_SSD);
        fp = fopen(dumppath, "wb");

        if (firstblockonly != 0) { // fake some SSD info
            ssdinfo.devs = 1;
            ssdinfo.blocks = 1;
        }
        printf("SSDINFO DEVS/BLOCKS = %d/%d\n", ssdinfo.devs, ssdinfo.blocks);

        // If provided, set the address
        // if (address != 0) {
        //     printf("Setting address to 0x%06x (%d)\n", address, address);

        //     wlen = portsend(&sd, 'j');
        //     if (wlen != 1) {
        //         printf("Error from write: %d, %d\n", wlen, errno);
        //     }
        //     wlen = portread(&sd, &buffer);
        //     if (wlen != 1) {
        //         printf("Error from read: %d, %d\n", wlen, errno);
        //     }
        //     if (buffer == '?') {
        //         // Send 4 bytes, LSB first
        //         for (i =0; i < 4; i++) {
        //             wlen = portsend(&sd, (address >> (i * 8)) | 0xFF);
        //         }
        //         if (wlen != 1) {
        //             printf("Error from write: %d, %d\n", wlen, errno);
        //         }
        //     }

        //     // Wait for OK
        //     wlen = portread(&sd, &buffer);
        //     if (wlen != 1) {
        //         printf("Error from read: %d, %d\n", wlen, errno);
        //     }
        //     if (buffer != '!') {
        //         printf("Arduino didn't like the address.\n");
        //         exit(-1);
        //     }

        // }

        unsigned long starttime = microsecond_time();

        for (curdev = 0; curdev < ssdinfo.devs; curdev++) {
            for (i = 0; i < ssdinfo.blocks; i++) {
                getblock(&sd, curblock, curdev, block);
                portsend(&sd, PORT_COMMAND_NEXT_BLOCK);
                curblock++;
                fwrite(&block, sizeof(unsigned char), sizeof(block), fp);
            }
            portsend(&sd, PORT_COMMAND_NEXT_DEVICE);
            curblock = 0;
        }

        unsigned long endtime = microsecond_time();

        double duration = ((double)(endtime - starttime)) / 1000.0; 
        printf("\n\nDump Duration: %f seconds\n", duration);

        double KBps = ((ssdinfo.blocks * 256.0) / duration) / 1000.0;
        printf("Transfer Speed: %f KBps\n", KBps);

        fclose(fp);
        printf("\n");
    }

    portclose(&sd);
}
