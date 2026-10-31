#include "unity.h"
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned int allocation_failure;
static int release_partial;

void setUp(void)
{
    fail_malloc_on_call(0);
    verify_child_before_exit(NULL);
}

void tearDown(void)
{
    fail_malloc_on_call(0);
    verify_child_before_exit(NULL);
}

static void fail_assertion(void)
{
    TEST_FAIL_MESSAGE("isolated assertion");
}

void test_given_child_assertion_failure_when_action_runs_then_parent_receives_failure_without_restarting_suite(void)
{
    // Given (Arrange)

    // When (Act)
    struct child_result result = run_child(fail_assertion);

    // Then (Assert)
    assert_child_failure(&result, 123, 0, "isolated assertion");
}

static void fail_exit_verification(void)
{
    TEST_FAIL_MESSAGE("unconsumed expectation at exit");
}

static void exit_unsuccessfully(void)
{
    exit(EXIT_FAILURE);
}

void test_given_unconsumed_expectations_when_child_exits_then_exit_is_not_mistaken_for_expected_failure(void)
{
    // Given (Arrange)
    verify_child_before_exit(fail_exit_verification);

    // When (Act)
    struct child_result result = run_child(exit_unsuccessfully);

    // Then (Assert)
    assert_child_failure(&result, 123, 0, "unconsumed expectation at exit");
}

static void allocate_until_failure(void)
{
    void *allocations[3] = {0};
    fail_malloc_on_call(allocation_failure);
    for (unsigned int i = 0; i < 3; i++)
    {
        allocations[i] = malloc(16);
        if (!allocations[i])
        {
            TEST_ASSERT_EQUAL_UINT(allocation_failure, i + 1);
            if (release_partial)
                for (unsigned int j = 0; j < i; j++)
                    free(allocations[j]);
            printf("injected allocation failure\n");
            exit(EXIT_FAILURE);
        }
    }
    TEST_FAIL_MESSAGE("allocation failure was not injected");
}

TEST_CASE(1)
TEST_CASE(2)
TEST_CASE(3)
void test_given_nth_allocation_failure_when_partial_allocations_are_freed_then_exit_audit_passes(
    unsigned int allocation)
{
    // Given (Arrange)
    allocation_failure = allocation;
    release_partial = 1;

    // When (Act)
    struct child_result result = run_child(allocate_until_failure);

    // Then (Assert)
    assert_child_failure(&result, EXIT_FAILURE, 0, "injected allocation failure");
}

void test_given_partial_allocation_leak_when_child_exits_then_exit_audit_rejects_failure(void)
{
    // Given (Arrange)
    allocation_failure = 2;
    release_partial = 0;

    // When (Act)
    struct child_result result = run_child(allocate_until_failure);

    // Then (Assert)
    assert_child_failure(&result, 124, 0, "Expected exactly one free before exit");
}
