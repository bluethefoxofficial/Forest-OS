#include <stddef.h>
#include "include/string.h"
#include "include/libc/string.h"
#include "include/memory_safe.h"

static size_t probe_guarded_span(const void* ptr, size_t length) {
    size_t span = memory_is_user_pointer(ptr)
                    ? memory_probe_user_buffer(ptr, length)
                    : memory_probe_buffer(ptr, length);
    if (span > length) {
        return length;
    }
    return span;
}

static size_t min_guarded_span(const void* a, const void* b, size_t length) {
    size_t span_a = probe_guarded_span(a, length);
    size_t span_b = probe_guarded_span(b, length);
    size_t span = (span_a < span_b) ? span_a : span_b;
    return (span < length) ? span : length;
}

uint16 strlength(const char* ch) {
    return (uint16)strlen(ch);

}



uint8 strEql(const char* ch1, const char* ch2) {
    uint16 size = strlength(ch1);
    if (size != strlength(ch2)) {
        return 0;
    }

    size_t span = min_guarded_span(ch1, ch2, (size_t)size + 1);
    if (span < (size_t)size + 1) {
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
    size_t total = 0;
    const char *cursor = s;
    while (true) {
        size_t window = probe_guarded_span(cursor, MEMORY_PAGE_SIZE);
        if (window == 0) {
            break;
        }
        for (size_t i = 0; i < window; i++) {
            if (cursor[i] == '\0') {
                return total + i;
            }
        }
        cursor += window;
        total += window;
    }
    return total;
}

char *strcpy(char *dest, const char *src) {
    char *ret = dest;
    size_t offset = 0;
    while (true) {
        size_t src_window = probe_guarded_span(src + offset, MEMORY_PAGE_SIZE);
        size_t dest_window = probe_guarded_span(dest + offset, MEMORY_PAGE_SIZE);
        size_t window = (src_window < dest_window) ? src_window : dest_window;
        if (window == 0) {
            break;
        }
        for (size_t i = 0; i < window; i++) {
            dest[offset + i] = src[offset + i];
            if (src[offset + i] == '\0') {
                return ret;
            }
        }
        offset += window;
    }
    return ret;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t allowed = min_guarded_span(dest, src, n);
    size_t i = 0;
    for (; i < allowed && src[i]; i++) {
        dest[i] = src[i];
    }
    for (; i < allowed; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char *strcat(char *dest, const char *src) {
    char *d = dest + strlen(dest);
    size_t offset = 0;
    while (true) {
        size_t src_window = probe_guarded_span(src + offset, MEMORY_PAGE_SIZE);
        size_t dest_window = probe_guarded_span(d + offset, MEMORY_PAGE_SIZE);
        size_t window = (src_window < dest_window) ? src_window : dest_window;
        if (window == 0) {
            break;
        }
        for (size_t i = 0; i < window; i++) {
            d[offset + i] = src[offset + i];
            if (src[offset + i] == '\0') {
                return dest;
            }
        }
        offset += window;
    }
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest + strlen(dest);
    size_t allowed = probe_guarded_span(d, n + 1);
    size_t src_allowed = probe_guarded_span(src, n);
    size_t limit = (allowed < src_allowed) ? allowed : src_allowed;
    if (limit > n) {
        limit = n;
    }
    size_t i = 0;
    for (; i < limit && src[i]; i++) {
        d[i] = src[i];
    }
    if (i < allowed) {
        d[i] = '\0';
    }
    return dest;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    size_t limit = min_guarded_span(s1, s2, n);
    for (size_t i = 0; i < limit; i++) {
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
    while (true) {
        size_t window = min_guarded_span(s1, s2, MEMORY_PAGE_SIZE);
        if (window == 0) {
            return 0;
        }
        for (size_t i = 0; i < window; i++) {
            unsigned char c1 = (unsigned char)s1[i];
            unsigned char c2 = (unsigned char)s2[i];
            if (c1 != c2) {
                return c1 - c2;
            }
            if (c1 == '\0') {
                return 0;
            }
        }
        s1 += window;
        s2 += window;
    }
}

char *strchr(const char *s, int c) {
    while (true) {
        size_t window = probe_guarded_span(s, MEMORY_PAGE_SIZE);
        if (window == 0) {
            return 0;
        }
        for (size_t i = 0; i < window; i++) {
            if (s[i] == (char)c) {
                return (char *)(s + i);
            }
            if (s[i] == '\0') {
                return (c == '\0') ? (char *)(s + i) : 0;
            }
        }
        s += window;
    }
}

char *strrchr(const char *s, int c) {
    const char *last = 0;
    while (true) {
        size_t window = probe_guarded_span(s, MEMORY_PAGE_SIZE);
        if (window == 0) {
            break;
        }
        for (size_t i = 0; i < window; i++) {
            if (s[i] == (char)c) {
                last = s + i;
            }
            if (s[i] == '\0') {
                return (c == '\0') ? (char *)(s + i) : (char *)last;
            }
        }
        s += window;
    }
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) {
        return (char *)haystack;
    }

    size_t needle_len = strlen(needle);
    const char *cursor = haystack;
    while (true) {
        size_t window = probe_guarded_span(cursor, MEMORY_PAGE_SIZE);
        if (window == 0) {
            break;
        }
        for (size_t i = 0; i < window; i++) {
            if (cursor[i] == '\0') {
                return 0;
            }
            size_t remain = window - i;
            size_t span = probe_guarded_span(cursor + i, needle_len);
            size_t cmp_len = (span < needle_len) ? span : needle_len;
            if (cmp_len < needle_len) {
                break;
            }
            if (strncmp(cursor + i, needle, needle_len) == 0) {
                return (char *)(cursor + i);
            }
            if (remain <= needle_len) {
                break;
            }
        }
        cursor += window;
    }
    return 0;
}

size_t strspn(const char *s, const char *accept) {
    size_t count = 0;
    while (true) {
        size_t window = probe_guarded_span(s, MEMORY_PAGE_SIZE);
        if (window == 0) {
            break;
        }
        for (size_t i = 0; i < window; i++) {
            if (s[i] == '\0') {
                return count;
            }
            if (!strchr(accept, s[i])) {
                return count;
            }
            count++;
        }
        s += window;
    }
    return count;
}

size_t strcspn(const char *s, const char *reject) {
    size_t count = 0;
    while (true) {
        size_t window = probe_guarded_span(s, MEMORY_PAGE_SIZE);
        if (window == 0) {
            break;
        }
        for (size_t i = 0; i < window; i++) {
            if (s[i] == '\0') {
                return count;
            }
            if (strchr(reject, s[i])) {
                return count;
            }
            count++;
        }
        s += window;
    }
    return count;
}

char *strpbrk(const char *s, const char *accept) {
    while (true) {
        size_t window = probe_guarded_span(s, MEMORY_PAGE_SIZE);
        if (window == 0) {
            return 0;
        }
        for (size_t i = 0; i < window; i++) {
            if (s[i] == '\0') {
                return 0;
            }
            if (strchr(accept, s[i])) {
                return (char *)(s + i);
            }
        }
        s += window;
    }
}

char *strtok(char *str, const char *delim) {
    static char *next = 0;
    if (str) {
        next = str;
    } else if (!next) {
        return 0;
    }

    // Skip leading delimiters.
    while (true) {
        size_t window = probe_guarded_span(next, MEMORY_PAGE_SIZE);
        if (window == 0) {
            next = 0;
            return 0;
        }
        size_t i = 0;
        for (; i < window && next[i]; i++) {
            if (!strchr(delim, next[i])) {
                break;
            }
        }
        next += i;
        if (i < window || !*next) {
            break;
        }
    }

    if (*next == '\0') {
        next = 0;
        return 0;
    }

    char *start = next;
    while (true) {
        size_t window = probe_guarded_span(next, MEMORY_PAGE_SIZE);
        if (window == 0) {
            next = 0;
            return start;
        }
        for (size_t i = 0; i < window; i++) {
            if (next[i] == '\0') {
                next = 0;
                return start;
            }
            if (strchr(delim, next[i])) {
                next[i] = '\0';
                next = next + i + 1;
                return start;
            }
        }
        next += window;
    }
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    size_t allowed = min_guarded_span(dest, src, n);
    if (d == s || allowed == 0) {
        return dest;
    }

    if (d < s) {
        for (size_t i = 0; i < allowed; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = allowed; i != 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    size_t limit = min_guarded_span(s1, s2, n);
    for (size_t i = 0; i < limit; i++) {
        if (a[i] != b[i]) {
            return (int)a[i] - (int)b[i];
        }
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char target = (unsigned char)c;
    size_t limit = probe_guarded_span(s, n);
    for (size_t i = 0; i < limit; i++) {
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
