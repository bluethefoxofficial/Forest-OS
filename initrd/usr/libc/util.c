#include "include/util.h"
#include <stddef.h>
#include "include/string.h"
#include "include/libc/string.h"
#include "include/libc/stdlib.h"
#ifndef USERSPACE_BUILD
#include "include/memory.h"
#endif

void memory_copy(const char *source, char *dest, int nbytes) {
    int i;
    for (i = 0; i < nbytes; i++) {
        *(dest + i) = *(source + i);             //    dest[i] = source[i]
    }
}

void memory_set(uint8 *dest, uint8 val, uint32 len) {
    uint8 *temp = (uint8 *)dest;
    for ( ; len != 0; len--) *temp++ = val;
}

void int_to_ascii(int n, char str[]) {          
    int i, sign;
    if ((sign = n) < 0) n = -n;
    i = 0;
    do {
        str[i++] = n % 10 + '0';         
    } while ((n /= 10) > 0);

    if (sign < 0) str[i++] = '-';
    str[i] = '\0';

    /* TODO: implement "reverse" */
}
string int_to_string(int n)
{
	string ch = malloc(50);
	int_to_ascii(n,ch);
	int len = strlength(ch);
	int i = 0;
	int j = len - 1;
	while(i<(len/2 + len%2))
	{
		char tmp = ch[i];
		ch[i] = ch[j];
		ch[j] = tmp;
		i++;
		j--;
	}
	return ch;
}

string long_to_string(long n) {
    string ch = malloc(50); // Allocate enough for a long
    int i = 0;
    long sign = n;

    if (sign == 0) {
        ch[i++] = '0';
        ch[i] = '\0';
        return ch;
    }

    if (sign < 0) {
        n = -n;
    }

    while (n != 0) {
        ch[i++] = (n % 10) + '0';
        n /= 10;
    }

    if (sign < 0) {
        ch[i++] = '-';
    }
    ch[i] = '\0';

    // Reverse the string
    int j = 0;
    i--; // Point to last character before null
    while(j < i) {
        char tmp = ch[j];
        ch[j] = ch[i];
        ch[i] = tmp;
        j++;
        i--;
    }
    return ch;
}

int str_to_int(string ch)
{
	int n = 0;
	int p = 1;
	int strlen = strlength(ch);
	int i;
	for (i = strlen-1;i>=0;i--)
	{
		n += ((int)(ch[i] - '0')) * p;
		p *= 10;
	}
	return n;
}
#ifndef USERSPACE_BUILD
void* malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    return kmalloc(size);
}

void free(void* ptr) {
    if (ptr) {
        kfree(ptr);
    }
}
#endif

void* calloc(size_t num, size_t size) {
    void* ptr = malloc(num * size);
    if (ptr) {
        // Assume memset is available from include/libc/string.h
        memset(ptr, 0, num * size);
    }
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    (void)ptr;
    (void)size;
    return NULL; // For now, no realloc
}

int atoi(const char *str) {
    if (!str) {
        return 0;
    }
    int sign = 1;
    int value = 0;
    while (*str == ' ' || *str == '\t' || *str == '\n') {
        str++;
    }
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    while (*str >= '0' && *str <= '9') {
        value = value * 10 + (*str - '0');
        str++;
    }
    return sign * value;
}

long atol(const char *str) {
    return (long)atoi(str);
}

double atof(const char *str) {
    return (double)atoi(str);
}

char *itoa(int value, char *str, int base) {
    static const char digits[] = "0123456789ABCDEF";
    if (base < 2 || base > 16 || !str) {
        return 0;
    }

    char *p = str;
    int temp = value;
    int sign = 0;

    if (value == 0) {
        *p++ = '0';
        *p = '\0';
        return str;
    }

    if (value < 0 && base == 10) {
        sign = 1;
        temp = -temp;
    }

    while (temp != 0) {
        int rem = temp % base;
        *p++ = digits[rem];
        temp /= base;
    }

    if (sign) {
        *p++ = '-';
    }
    *p = '\0';

    // Reverse the string in-place.
    for (char *start = str, *end = p - 1; start < end; start++, end--) {
        char c = *start;
        *start = *end;
        *end = c;
    }
    return str;
}

void exit(int status) {
    (void)status;
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

void abort(void) {
    exit(-1);
}

int system(const char *command) {
    (void)command;
    return -1;
}

static uint32 rand_seed = 1;

int rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (int)((rand_seed >> 16) & 0x7FFF);
}

void srand(unsigned int seed) {
    rand_seed = seed ? seed : 1;
}

int abs(int n) {
    return (n < 0) ? -n : n;
}

long labs(long n) {
    return (n < 0) ? -n : n;
}

div_t div(int numer, int denom) {
    div_t d;
    d.quot = numer / denom;
    d.rem = numer % denom;
    return d;
}

void *bsearch(const void *key, const void *base, size_t num, size_t size,
              int (*compare)(const void *, const void *)) {
    size_t left = 0;
    size_t right = num;
    while (left < right) {
        size_t mid = left + (right - left) / 2;
        const void *elem = (const uint8 *)base + mid * size;
        int cmp = compare(key, elem);
        if (cmp == 0) {
            return (void *)elem;
        } else if (cmp < 0) {
            right = mid;
        } else {
            left = mid + 1;
        }
    }
    return 0;
}

void qsort(void *base, size_t num, size_t size,
           int (*compare)(const void *, const void *)) {
    // Simple bubble sort to keep footprint tiny.
    uint8 *array = (uint8 *)base;
    for (size_t i = 0; i < num; i++) {
        for (size_t j = 0; j + 1 < num - i; j++) {
            uint8 *a = array + j * size;
            uint8 *b = array + (j + 1) * size;
            if (compare(a, b) > 0) {
                for (size_t k = 0; k < size; k++) {
                    uint8 tmp = a[k];
                    a[k] = b[k];
                    b[k] = tmp;
                }
            }
        }
    }
}
