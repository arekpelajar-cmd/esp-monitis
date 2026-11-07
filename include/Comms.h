#ifndef COMMS_H
#define COMMS_H

#include <Arduino.h>

void initComms();
void taskCommsCode(void *pvParameters);
bool isMqttConnected();

#endif
