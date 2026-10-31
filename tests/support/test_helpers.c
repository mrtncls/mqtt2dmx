#include "test_helpers.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static void *watched[6];
static unsigned int freed[6];
static size_t watched_count;
static unsigned int allocation_countdown;
static void (*child_exit_verifier)(void);

void *__real_malloc(size_t size);
void __real_free(void *pointer);
void __real_exit(int status) __attribute__((noreturn));

void __wrap_exit(int status)
{
    if (child_exit_verifier)
        child_exit_verifier();
    for (size_t i = 0; i < watched_count; i++)
        if (freed[i] != 1)
        {
            fprintf(stderr, "Expected exactly one free before exit\n");
            __real_exit(124);
        }
    __real_exit(status);
}

void fail_malloc_on_call(unsigned int call)
{
    watch_frees(NULL, 0);
    allocation_countdown = call;
}

void *__wrap_malloc(size_t size)
{
    if (!allocation_countdown)
        return __real_malloc(size);
    if (--allocation_countdown == 0)
        return NULL;
    void *pointer = __real_malloc(size);
    if (pointer)
    {
        TEST_ASSERT_LESS_THAN(6, watched_count);
        watched[watched_count] = pointer;
        freed[watched_count++] = 0;
    }
    return pointer;
}

void verify_child_before_exit(void (*verify)(void))
{
    child_exit_verifier = verify;
}

void __wrap_free(void *pointer)
{
    for (size_t i = 0; i < watched_count; i++)
        if (pointer == watched[i])
            freed[i]++;
    __real_free(pointer);
}

void watch_frees(void **pointers, size_t count)
{
    TEST_ASSERT_LESS_OR_EQUAL(6, count);
    watched_count = count;
    memset(freed, 0, sizeof(freed));
    if (count)
        memcpy(watched, pointers, count * sizeof(void *));
}

void assert_watched_frees(void)
{
    size_t count = watched_count;
    watched_count = 0;
    for (size_t i = 0; i < count; i++)
        TEST_ASSERT_EQUAL_UINT(1, freed[i]);
}

void assert_no_watched_frees(void)
{
    for (size_t i = 0; i < watched_count; i++)
        TEST_ASSERT_EQUAL_UINT(0, freed[i]);
}

struct child_result run_child(void (*action)(void))
{
    int descriptors[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(descriptors));
    fflush(NULL);
    pid_t child = fork();
    TEST_ASSERT_TRUE(child >= 0);
    if (child == 0)
    {
        struct rlimit limit = {0, 0};
        setrlimit(RLIMIT_CORE, &limit);
        alarm(3);
        close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0 ||
            dup2(descriptors[1], STDERR_FILENO) < 0)
            _exit(125);
        close(descriptors[1]);
        setvbuf(stdout, NULL, _IONBF, 0);
        // Keep Unity's longjmp inside the child instead of resuming the parent runner.
        if (TEST_PROTECT())
        {
            action();
            if (child_exit_verifier)
                child_exit_verifier();
        }
        _exit(Unity.CurrentTestFailed ? 123 : 0);
    }

    close(descriptors[1]);
    struct child_result result;
    size_t used = 0;
    ssize_t bytes;
    while (used < sizeof(result.output) - 1 &&
           (bytes = read(descriptors[0], result.output + used,
                         sizeof(result.output) - 1 - used)) > 0)
        used += (size_t)bytes;
    result.output[used] = '\0';
    close(descriptors[0]);
    TEST_ASSERT_EQUAL_INT(child, waitpid(child, &result.status, 0));
    child_exit_verifier = NULL;
    return result;
}

static void assert_child_exit(const struct child_result *result, int expected)
{
    char message[4352];
    snprintf(message, sizeof(message), "Expected exit %d; child exit %d, signal %d.\n%s",
             expected, WIFEXITED(result->status) ? WEXITSTATUS(result->status) : -1,
             WIFSIGNALED(result->status) ? WTERMSIG(result->status) : 0, result->output);
    TEST_ASSERT_TRUE_MESSAGE(WIFEXITED(result->status), message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected, WEXITSTATUS(result->status), message);
}

void assert_child_success(const struct child_result *result)
{
    assert_child_exit(result, 0);
}

void assert_child_failure(const struct child_result *result, int exit_code,
                          int signal_number, const char *diagnostic)
{
    int status = result->status;
    const char *output = result->output;
    if (signal_number)
    {
        TEST_ASSERT_TRUE_MESSAGE(WIFSIGNALED(status), output);
        TEST_ASSERT_EQUAL_INT(signal_number, WTERMSIG(status));
    }
    else
    {
        assert_child_exit(result, exit_code);
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(output, diagnostic), output);
}

void expect_child_failure(void (*action)(void), int exit_code, int signal_number,
                          const char *diagnostic)
{
    struct child_result result = run_child(action);
    assert_child_failure(&result, exit_code, signal_number, diagnostic);
}
