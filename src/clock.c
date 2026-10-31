#include "clock.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int Clock_Now(uint64_t *milliseconds)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        perror("clock_gettime");
        return EXIT_FAILURE;
    }
    *milliseconds = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
    return EXIT_SUCCESS;
}

int Clock_Wait(unsigned int milliseconds)
{
    struct timespec delay = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    while (nanosleep(&delay, &delay) != 0)
    {
        if (errno != EINTR)
        {
            perror("nanosleep");
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
