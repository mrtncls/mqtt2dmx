#ifndef paho_fake_h
#define paho_fake_h

#include <stddef.h>

void PahoFake_Reset(void);
int PahoFake_Deliver(unsigned int number);
size_t PahoFake_ReleasedMessages(void);
size_t PahoFake_ReleasedTopics(void);

#endif
