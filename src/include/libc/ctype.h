#ifndef LIBC_CTYPE_H
#define LIBC_CTYPE_H

static inline int isalpha(int c) {
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

static inline int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

static inline int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

static inline int isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

static inline int isupper(int c) {
    return (c >= 'A' && c <= 'Z');
}

static inline int islower(int c) {
    return (c >= 'a' && c <= 'z');
}

static inline int tolower(int c) {
    return isupper(c) ? (c - 'A' + 'a') : c;
}

static inline int toupper(int c) {
    return islower(c) ? (c - 'a' + 'A') : c;
}

#endif
