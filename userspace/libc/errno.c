/*
 * errno.c - Error number implementation for Forest OS libc
 */

#include "../../src/include/libc/errno.h"
#include "../../src/include/libc/string.h"
#include "../../src/include/libc/unistd.h"

/* Global errno variable */
int errno = 0;

// Additional errno-related functions for better compatibility
int *__errno_location(void) {
    return &errno;
}

// Error message strings for strerror()
static const char *error_messages[] = {
    "Success",
    "Operation not permitted",           // EPERM=1
    "No such file or directory",         // ENOENT=2
    "No such process",                   // ESRCH=3
    "Interrupted system call",           // EINTR=4
    "Input/output error",                 // EIO=5
    "No such device or address",         // ENXIO=6
    "Argument list too long",            // E2BIG=7
    "Exec format error",                 // ENOEXEC=8
    "Bad file descriptor",                // EBADF=9
    "No child processes",                 // ECHILD=10
    "Resource temporarily unavailable",  // EAGAIN=11
    "Not enough space",                  // ENOMEM=12
    "Permission denied",                  // EACCES=13
    "Bad address",                       // EFAULT=14
    "Device or resource busy",            // EBUSY=16
    "File exists",                       // EEXIST=17
    "Invalid cross-device link",         // EXDEV=18
    "No such device",                    // ENODEV=19
    "Not a directory",                   // ENOTDIR=20
    "Is a directory",                    // EISDIR=21
    "Invalid argument",                   // EINVAL=22
    "Too many open files in system",     // ENFILE=23
    "Too many open files",               // EMFILE=24
    "Inappropriate ioctl for device",    // ENOTTY=25
    "Text file busy",                    // ETXTBSY=26
    "File too large",                    // EFBIG=27
    "No space left on device",           // ENOSPC=28
    "Illegal seek",                      // ESPIPE=29
    "Read-only file system",             // EROFS=30
    "Too many links",                    // EMLINK=31
    "Broken pipe",                       // EPIPE=32
    "Numerical argument out of domain",  // EDOM=33
    "Numerical result out of range",     // ERANGE=34
    "Function not implemented",          // ENOSYS=38
};

char *strerror(int errnum) {
    if (errnum == 0) return "Success";
    if (errnum >= 1 && errnum <= 34) {
        switch (errnum) {
            case 1: return (char*)error_messages[1];
            case 2: return (char*)error_messages[2];
            case 3: return (char*)error_messages[3];
            case 4: return (char*)error_messages[4];
            case 5: return (char*)error_messages[5];
            case 6: return (char*)error_messages[6];
            case 7: return (char*)error_messages[7];
            case 8: return (char*)error_messages[8];
            case 9: return (char*)error_messages[9];
            case 10: return (char*)error_messages[10];
            case 11: return (char*)error_messages[11];
            case 12: return (char*)error_messages[12];
            case 13: return (char*)error_messages[13];
            case 14: return (char*)error_messages[14];
            case 16: return (char*)error_messages[15];
            case 17: return (char*)error_messages[16];
            case 18: return (char*)error_messages[17];
            case 19: return (char*)error_messages[18];
            case 20: return (char*)error_messages[19];
            case 21: return (char*)error_messages[20];
            case 22: return (char*)error_messages[21];
            case 23: return (char*)error_messages[22];
            case 24: return (char*)error_messages[23];
            case 25: return (char*)error_messages[24];
            case 26: return (char*)error_messages[25];
            case 27: return (char*)error_messages[26];
            case 28: return (char*)error_messages[27];
            case 29: return (char*)error_messages[28];
            case 30: return (char*)error_messages[29];
            case 31: return (char*)error_messages[30];
            case 32: return (char*)error_messages[31];
            case 33: return (char*)error_messages[32];
            case 34: return (char*)error_messages[33];
            case 38: return (char*)error_messages[34];
        }
    }
    return "Unknown error";
}

void perror(const char *s) {
    const char *err_str = strerror(errno);
    if (s && *s) {
        write(2, s, strlen(s));
        write(2, ": ", 2);
    }
    write(2, err_str, strlen(err_str));
    write(2, "\n", 1);
}