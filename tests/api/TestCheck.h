/*
 * The whole test framework: a macro, a counter, and a non-zero exit. See tests/README.md.
 */

#ifndef _MASSIF_TESTS_TESTCHECK_H_
#define _MASSIF_TESTS_TESTCHECK_H_

#include <cstdio>

extern int failures;

#define TEST_CHECK(condition, what)                                              \
    do {                                                                         \
        if (condition) {                                                         \
            std::printf("ok    %s\n", what);                                     \
        } else {                                                                 \
            std::printf("FAIL  %s (%s:%d)\n", what, __FILE__, __LINE__);         \
            failures++;                                                          \
        }                                                                        \
    } while (false)

#endif
