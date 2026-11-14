/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MD_NATIVE_TEST_H
#define MD_NATIVE_TEST_H
#include <stdio.h>
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)
#define RUN(test) do { \
    if (test()) return 1; \
    printf("PASS %s\n", #test); \
} while (0)
#endif
