/*
 * harness.h — minimal host test harness for EdgeSense C units.
 *
 * The pre-existing projects in this portfolio use small hand-written C test
 * binaries rather than a framework, for a reason worth keeping: a test binary
 * that needs no framework can be compiled and run by a shell script on any
 * machine, including one where nothing is installed yet. EdgeSense keeps that
 * property for its portable-C units.
 *
 * Usage:
 *
 *   #include "harness.h"
 *
 *   int main(void)
 *   {
 *       CHECK(1 + 1 == 2);
 *       CHECK_STR_EQ(edge_channel_name(EDGE_CH_SOIL), "soil");
 *       return HARNESS_REPORT("edge_channel");
 *   }
 *
 * HARNESS_REPORT prints a machine-greppable summary line and returns a process
 * exit status, so a shell script (or CI) can depend on it.
 */
#ifndef EDGE_TEST_HARNESS_H
#define EDGE_TEST_HARNESS_H

#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned edge_checks = 0u;
static unsigned edge_failures = 0u;

#define CHECK(cond)                                                            \
    do {                                                                       \
        edge_checks += 1u;                                                     \
        if (!(cond)) {                                                         \
            edge_failures += 1u;                                               \
            (void)printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                      \
    } while (0)

#define CHECK_UINT_EQ(actual, expected)                                        \
    do {                                                                       \
        unsigned long edge_a = (unsigned long)(actual);                        \
        unsigned long edge_e = (unsigned long)(expected);                      \
        edge_checks += 1u;                                                     \
        if (edge_a != edge_e) {                                                \
            edge_failures += 1u;                                               \
            (void)printf("    FAIL %s:%d  %s == %s (got %lu, want %lu)\n",     \
                         __FILE__, __LINE__, #actual, #expected, edge_a,       \
                         edge_e);                                              \
        }                                                                      \
    } while (0)

#define CHECK_INT_EQ(actual, expected)                                         \
    do {                                                                       \
        long edge_a = (long)(actual);                                          \
        long edge_e = (long)(expected);                                        \
        edge_checks += 1u;                                                     \
        if (edge_a != edge_e) {                                                \
            edge_failures += 1u;                                               \
            (void)printf("    FAIL %s:%d  %s == %s (got %ld, want %ld)\n",     \
                         __FILE__, __LINE__, #actual, #expected, edge_a,       \
                         edge_e);                                              \
        }                                                                      \
    } while (0)

#define CHECK_STR_EQ(actual, expected)                                         \
    do {                                                                       \
        const char *edge_a = (actual);                                         \
        const char *edge_e = (expected);                                       \
        edge_checks += 1u;                                                     \
        if (edge_a == NULL || strcmp(edge_a, edge_e) != 0) {                   \
            edge_failures += 1u;                                               \
            (void)printf("    FAIL %s:%d  %s == \"%s\" (got \"%s\")\n",        \
                         __FILE__, __LINE__, #actual, edge_e,                  \
                         edge_a ? edge_a : "(null)");                          \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(actual, expected, tol)                                      \
    do {                                                                       \
        double edge_a = (double)(actual);                                      \
        double edge_e = (double)(expected);                                    \
        edge_checks += 1u;                                                     \
        if (!(fabs(edge_a - edge_e) <= (double)(tol))) {                       \
            edge_failures += 1u;                                               \
            (void)printf("    FAIL %s:%d  %s ~= %s (got %.6f, want %.6f)\n",   \
                         __FILE__, __LINE__, #actual, #expected, edge_a,       \
                         edge_e);                                              \
        }                                                                      \
    } while (0)

/* Returns the process exit status: 0 when every check passed, 1 otherwise. */
#define HARNESS_REPORT(suite)                                                  \
    ((void)printf("  %s: %u checks, %u failed\n", (suite), edge_checks,        \
                  edge_failures),                                              \
     (edge_failures == 0u) ? 0 : 1)

#endif /* EDGE_TEST_HARNESS_H */
