#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>

void initSensors();
int getDropCount_Safe(int ch);
void handleEncoderInput();

#endif
