#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stddef.h>

void watch_frees(void **pointers, size_t count);
void assert_watched_frees(void);
void assert_no_watched_frees(void);
void fail_malloc_on_call(unsigned int call);
void verify_child_before_exit(void (*verify)(void));
struct child_result
{
    int status;
    char output[4096];
};

struct child_result run_child(void (*action)(void));
void assert_child_success(const struct child_result *result);
void assert_child_failure(const struct child_result *result, int exit_code,
                          int signal_number, const char *diagnostic);
void expect_child_failure(void (*action)(void), int exit_code, int signal_number,
                          const char *diagnostic);

#endif
