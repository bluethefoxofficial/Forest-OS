#ifndef ERRNO_DEFS_H
#define ERRNO_DEFS_H

/* Standard error codes for Forest OS */
#define EPERM        1  /* Operation not permitted */
#define ENOENT       2  /* No such file or directory */
#define EIO          5  /* I/O error */
#define ENOMEM      12  /* Out of memory */
#define EBUSY       16  /* Device or resource busy */
#define ENODEV      19  /* No such device */
#define EINVAL      22  /* Invalid argument */
#define ENOSPC      28  /* No space left on device */
#define ERANGE      34  /* Result too large */
#define EOVERFLOW   75  /* Value too large for defined data type */
#define ENOTSUP     95  /* Operation not supported */
#define EOPNOTSUPP  95  /* Operation not supported (alias) */

#endif /* ERRNO_DEFS_H */
