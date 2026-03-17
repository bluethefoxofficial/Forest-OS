#ifndef LIBC_STDBOOL_H
#define LIBC_STDBOOL_H

typedef unsigned char bool;

#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

#define __bool_true_false_are_defined 1

#endif
