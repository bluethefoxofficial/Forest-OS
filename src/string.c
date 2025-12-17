#include <stddef.h>
#include "include/string.h"
#include "include/libc/string.h"

uint16 strlength(const char* ch) {

    uint16 i = 0;

    while (ch[i] != '\0') {

        i++;

    }

    return i;

}



uint8 strEql(const char* ch1, const char* ch2) {
    uint8 size = strlength(ch1);
    if (size != strlength(ch2)) {
        return 0;
    }

    for (uint8 i = 0; i <= size; i++) {
        if (ch1[i] != ch2[i]) {
            return 0;
        }
    }
    return 1;
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) {
        p++;
    }
    return (size_t)(p - s);
}

char *strcpy(char *dest, const char *src) {
    char *ret = dest;
    while ((*dest++ = *src++)) {
    }
    return ret;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char *strcat(char *dest, const char *src) {
    char *d = dest + strlen(dest);
    while ((*d++ = *src++)) {
    }
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest + strlen(dest);
    size_t i = 0;
    while (i < n && src[i]) {
        d[i] = src[i];
        i++;
    }
    d[i] = '\0';
    return dest;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c1 = (unsigned char)s1[i];
        unsigned char c2 = (unsigned char)s2[i];
        if (c1 != c2) {
            return c1 - c2;
        }
        if (c1 == '\0') {
            return 0;
        }
    }
    return 0;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    return (c == '\0') ? (char *)s : 0;
}

char *strrchr(const char *s, int c) {
    const char *last = 0;
    while (*s) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    if (c == '\0') {
        return (char *)s;
    }
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) {
        return (char *)haystack;
    }
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (*n == '\0') {
            return (char *)haystack;
        }
    }
    return 0;
}

size_t strspn(const char *s, const char *accept) {
    size_t count = 0;
    for (; *s; s++) {
        if (!strchr(accept, *s)) {
            break;
        }
        count++;
    }
    return count;
}

size_t strcspn(const char *s, const char *reject) {
    size_t count = 0;
    for (; *s; s++) {
        if (strchr(reject, *s)) {
            break;
        }
        count++;
    }
    return count;
}

char *strpbrk(const char *s, const char *accept) {
    for (; *s; s++) {
        if (strchr(accept, *s)) {
            return (char *)s;
        }
    }
    return 0;
}

char *strtok(char *str, const char *delim) {
    static char *next = 0;
    if (str) {
        next = str;
    } else if (!next) {
        return 0;
    }

    // Skip leading delimiters.
    while (*next && strchr(delim, *next)) {
        next++;
    }
    if (*next == '\0') {
        next = 0;
        return 0;
    }

    char *start = next;
    while (*next && !strchr(delim, *next)) {
        next++;
    }

    if (*next) {
        *next = '\0';
        next++;
    } else {
        next = 0;
    }
    return start;
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) {
        return dest;
    }

    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i != 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return (int)a[i] - (int)b[i];
        }
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char target = (unsigned char)c;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == target) {
            return (void *)(p + i);
        }
    }
    return 0;
}

char *strerror(int errnum) {
    // Minimal strerror for debugging; maps a few common codes.
    switch (errnum) {
        case 0: return "no error";
        case 1: return "operation not permitted";
        case 2: return "no such file or directory";
        case 12: return "out of memory";
        default: return "unknown error";
    }
}
