/*
 * The few C library functions the application uses (it links without a C
 * library; the original takes them from the IAR runtime: strlen 0x08023CA0).
 */
#include <stddef.h>
#include <string.h>

size_t strlen(const char *s)
{
    const char *p = s;

    while (*p != 0) {
        p++;
    }
    return (size_t)(p - s);
}
