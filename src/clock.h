#ifndef clock_h
#define clock_h

#include <stdint.h>

int Clock_Now(uint64_t *milliseconds);
int Clock_Wait(unsigned int milliseconds);

#endif
