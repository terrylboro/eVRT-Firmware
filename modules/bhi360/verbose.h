#ifndef EVRT_BHI360_VERBOSE_COMPAT_H
#define EVRT_BHI360_VERBOSE_COMPAT_H

#include <stdint.h>

#define PRINT(format, ...)    do { } while (0)
#define INFO(format, ...)     do { } while (0)
#define PRINT_I(format, ...)  do { } while (0)
#define WARNING(format, ...)  do { } while (0)
#define PRINT_W(format, ...)  do { } while (0)
#define ERROR(format, ...)    do { } while (0)
#define PRINT_E(format, ...)  do { } while (0)
#define DATA(format, ...)     do { } while (0)
#define PRINT_D(format, ...)  do { } while (0)
#define HEX(format, ...)      do { } while (0)
#define PRINT_H(format, ...)  do { } while (0)

static inline void verbose_write(uint8_t *buffer, uint16_t length)
{
    (void)buffer;
    (void)length;
}

#endif
