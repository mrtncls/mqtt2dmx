#ifndef application_h
#define application_h

#include "mqtt.h"

void Application_Reset(void);
bool Application_IsStopping(void);
int Application_WriteFrame(void);
void handle_signal(int sig);
void onReceived(MQTTMessage *message);

#endif
