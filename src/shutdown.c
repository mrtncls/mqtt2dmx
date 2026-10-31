#include "shutdown.h"
#include "clock.h"
#include "dmx.h"
#include "mqtt.h"
#include <stdio.h>
#include <stdlib.h>

int Shutdown_Run(int status)
{
    uint64_t start, now;
    if (status == EXIT_SUCCESS && !MQTT_IsClosed())
    {
        if (Clock_Now(&start) != EXIT_SUCCESS || MQTT_Stop() != EXIT_SUCCESS)
            status = EXIT_FAILURE;
        while (status == EXIT_SUCCESS && !MQTT_IsClosed())
        {
            if (Clock_Now(&now) != EXIT_SUCCESS)
                status = EXIT_FAILURE;
            else if (now - start >= 5000)
            {
                fprintf(stderr, "MQTT shutdown timed out after 5000 ms\n");
                status = EXIT_FAILURE;
            }
            else if (MQTT_Poll() != EXIT_SUCCESS || Clock_Wait(1) != EXIT_SUCCESS)
                status = EXIT_FAILURE;
        }
    }
    MQTT_Close();
    if (MQTT_HasFailed())
        status = EXIT_FAILURE;
    if (DMX_Close() != EXIT_SUCCESS)
        status = EXIT_FAILURE;
    return status;
}
