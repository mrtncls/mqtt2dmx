#ifndef TEST_PAHO_API_H
#define TEST_PAHO_API_H

#include "MQTTAsync.h"

int MQTTAsync_create(MQTTAsync *handle, const char *serverURI, const char *clientId,
                     int persistence_type, void *persistence_context);
int MQTTAsync_setCallbacks(MQTTAsync handle, void *context, MQTTAsync_connectionLost *cl,
                           MQTTAsync_messageArrived *ma, MQTTAsync_deliveryComplete *dc);
int MQTTAsync_setConnected(MQTTAsync handle, void *context, MQTTAsync_connected *co);
int MQTTAsync_connect(MQTTAsync handle, const MQTTAsync_connectOptions *options);
int MQTTAsync_subscribe(MQTTAsync handle, const char *topic, int qos,
                        MQTTAsync_responseOptions *response);
int MQTTAsync_disconnect(MQTTAsync handle, const MQTTAsync_disconnectOptions *options);
int MQTTAsync_sendMessage(MQTTAsync handle, const char *destinationName,
                          const MQTTAsync_message *msg, MQTTAsync_responseOptions *response);
void MQTTAsync_freeMessage(MQTTAsync_message **msg);
void MQTTAsync_free(void *ptr);
void MQTTAsync_destroy(MQTTAsync *handle);

#endif
