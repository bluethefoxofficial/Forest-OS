#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include "types.h"

void memory_copy(char *source, char *dest, int nbytes);
void memory_set(uint8 *dest, uint8 val, uint32 len);
void int_to_ascii(int n, char str[]);  
void *malloc(size_t nbytes);
void free(void *ptr);
//int
int strncmp( const char * s1, const char * s2, size_t n );
int str_to_int(string ch)  ;

//string
string int_to_string(int n);
string long_to_string(long n);


//char
char **split(char *string, const char delimiter);
//bool related
bool StartsWith(const char *a, const char *b);

#endif
