#include <stdint.h>
#include "libc/errno.h"
#include "../../src/include/libc/stdlib.h"
#include "../../src/include/libc/unistd.h"
#include "../../src/include/libc/string.h"

static unsigned int rand_seed = 1;
static uint8_t* heap_break = NULL;
static char default_path_env[] = "PATH=/bin";
static char *default_environ[] = { default_path_env, NULL };
char **environ = default_environ;
static size_t env_count = 1;
static size_t env_capacity = 2;

static int _errno_value = 0;

int *__errno_location(void) {
    return &_errno_value;
}

static int find_env_index(const char *name, size_t name_len) {
    if (!environ) {
        return -1;
    }
    for (size_t i = 0; i < env_count; i++) {
        const char *entry = environ[i];
        if (!entry) {
            continue;
        }
        if (strncmp(entry, name, name_len) == 0 && entry[name_len] == '=') {
            return (int)i;
        }
    }
    return -1;
}

static int ensure_env_capacity(size_t needed) {
    if (needed <= env_capacity) {
        return 0;
    }
    size_t new_capacity = env_capacity ? env_capacity : 2;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    char **new_env = malloc(new_capacity * sizeof(char *));
    if (!new_env) {
        return -1;
    }
    for (size_t i = 0; i < env_count; i++) {
        new_env[i] = environ ? environ[i] : NULL;
    }
    new_env[env_count] = NULL;
    environ = new_env;
    env_capacity = new_capacity;
    return 0;
}

static char *dup_env_string(const char *name, size_t name_len, const char *value) {
    size_t value_len = strlen(value);
    size_t total = name_len + 1 + value_len + 1;
    char *entry = malloc(total);
    if (!entry) {
        return NULL;
    }
    memcpy(entry, name, name_len);
    entry[name_len] = '=';
    memcpy(entry + name_len + 1, value, value_len);
    entry[name_len + 1 + value_len] = '\0';
    return entry;
}

static uint8_t* ensure_heap_initialized(void) {
    if (!heap_break) {
        heap_break = (uint8_t*)brk(0);
    }
    return heap_break;
}

void *malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    size = (size + 15) & ~15;
    uint8_t* base = ensure_heap_initialized();
    if (!base) {
        return NULL;
    }
    uint8_t* new_break = heap_break + size;
    if (brk(new_break) < 0) {
        return NULL;
    }
    void* ptr = heap_break;
    heap_break = new_break;
    return ptr;
}

void free(void *ptr) {
    (void)ptr; // Simple bump allocator does not support free yet.
}

void *calloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    void* new_ptr = malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, size);
    }
    return new_ptr;
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

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') {
        s++;
    }

    int sign = 1;
    if (*s == '+' || *s == '-') {
        if (*s == '-') sign = -1;
        s++;
    }

    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
            s += 1;
        } else {
            base = 10;
        }
    }

    long result = 0;
    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (*s >= 'a' && *s <= 'z') {
            digit = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'Z') {
            digit = *s - 'A' + 10;
        } else {
            break;
        }
        if (digit >= base) {
            break;
        }
        result = result * base + digit;
        s++;
    }

    if (endptr) {
        *endptr = (char *)s;
    }
    return result * sign;
}

char *itoa(int value, char *str, int base) {
    if (base < 2 || base > 16) {
        str[0] = '\0';
        return str;
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
        *p++ = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        temp /= base;
    }

    if (sign) {
        *p++ = '-';
    }
    *p = '\0';

    for (char *start = str, *end = p - 1; start < end; start++, end--) {
        char c = *start;
        *start = *end;
        *end = c;
    }
    return str;
}

void exit(int status) {
    _exit(status);
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

int rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (int)((rand_seed >> 16) & 0x7FFF);
}

void srand(unsigned int seed) {
    rand_seed = seed;
}

int abs(int n) {
    return n < 0 ? -n : n;
}

long labs(long n) {
    return n < 0 ? -n : n;
}

div_t div(int numer, int denom) {
    div_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

char *getenv(const char *name) {
    if (!name || !*name) {
        return NULL;
    }
    size_t len = strlen(name);
    int idx = find_env_index(name, len);
    if (idx < 0) {
        return NULL;
    }
    return environ[idx] + len + 1;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !*name || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    if (!value) {
        value = "";
    }
    size_t name_len = strlen(name);
    int idx = find_env_index(name, name_len);
    if (idx >= 0 && !overwrite) {
        return 0;
    }
    char *entry = dup_env_string(name, name_len, value);
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }
    if (idx >= 0) {
        environ[idx] = entry;
        return 0;
    }
    if (ensure_env_capacity(env_count + 2) < 0) {
        free(entry);
        errno = ENOMEM;
        return -1;
    }
    environ[env_count++] = entry;
    environ[env_count] = NULL;
    return 0;
}

int unsetenv(const char *name) {
    if (!name || !*name || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    size_t name_len = strlen(name);
    int idx = find_env_index(name, name_len);
    if (idx < 0) {
        return 0;
    }
    for (size_t i = (size_t)idx; i + 1 < env_count; i++) {
        environ[i] = environ[i + 1];
    }
    env_count--;
    environ[env_count] = NULL;
    return 0;
}

int putenv(char *string) {
    if (!string || !*string) {
        errno = EINVAL;
        return -1;
    }
    char *eq = strchr(string, '=');
    if (!eq) {
        errno = EINVAL;
        return -1;
    }
    size_t name_len = (size_t)(eq - string);
    int idx = find_env_index(string, name_len);
    if (idx >= 0) {
        environ[idx] = string;
        return 0;
    }
    if (ensure_env_capacity(env_count + 2) < 0) {
        errno = ENOMEM;
        return -1;
    }
    environ[env_count++] = string;
    environ[env_count] = NULL;
    return 0;
}
