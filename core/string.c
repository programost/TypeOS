/*
 * string.c - minimal freestanding string and memory routines.
 */

#include "include/string.h"

int
strcmp (const char *s1, const char *s2)
{
        while (*s1 && (*s1 == *s2)) {
                s1++;
                s2++;
        }

        return *(const unsigned char *) s1 - *(const unsigned char *) s2;
}

int
strncmp (const char *s1, const char *s2, size_t n)
{
        if (n == 0) {
                return 0;
        }

        while (n-- > 0) {
                unsigned char c1 = (unsigned char) *s1++;
                unsigned char c2 = (unsigned char) *s2++;

                if (c1 != c2) {
                        return c1 - c2;
                }
                if (c1 == '\0') {
                        return 0;
                }
        }

        return 0;
}

size_t
strlen (const char *s)
{
        size_t len = 0;

        while (s[len] != '\0') {
                len++;
        }

        return len;
}

char *
strcpy (char *dest, const char *src)
{
        char *d = dest;

        while ((*d++ = *src++) != '\0') {
                ;
        }

        return dest;
}

char *
strncpy (char *dest, const char *src, size_t n)
{
        size_t i;

        for (i = 0; i < n && src[i] != '\0'; i++) {
                dest[i] = src[i];
        }

        for (; i < n; i++) {
                dest[i] = '\0';
        }

        return dest;
}

void *
memcpy (void *dest, const void *src, size_t n)
{
        unsigned char *d = (unsigned char *) dest;
        const unsigned char *s = (const unsigned char *) src;

        for (size_t i = 0; i < n; i++) {
                d[i] = s[i];
        }

        return dest;
}

void *
memset (void *s, int c, size_t n)
{
        unsigned char *p = (unsigned char *) s;

        for (size_t i = 0; i < n; i++) {
                p[i] = (unsigned char) c;
        }

        return s;
}

char *
strrchr (const char *s, int c)
{
        const char *last = NULL;

        while (*s != '\0') {
                if (*s == (char) c) {
                        last = s;
                }
                s++;
        }

        if ((char) c == '\0') {
                return (char *) s;
        }

        return (char *) last;
}

int
memcmp (const void *s1, const void *s2, size_t n)
{
        const unsigned char *a = (const unsigned char *) s1;
        const unsigned char *b = (const unsigned char *) s2;

        for (size_t i = 0; i < n; i++) {
                if (a[i] != b[i]) {
                        return a[i] - b[i];
                }
        }

        return 0;
}
