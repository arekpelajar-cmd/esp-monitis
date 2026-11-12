#ifndef CONTROL_H
#define CONTROL_H

#include <Arduino.h>

void initControl();
void taskControlCode(void *pvParameters);
bool preparePumpOutputForInactiveState(int channel);

#endif
