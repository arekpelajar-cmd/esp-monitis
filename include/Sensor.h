#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>

void initSensors();
int getDropCount_Safe(int ch);
size_t getDropTimestamps_Safe(int ch, uint32_t *timestamps, size_t capacity);
void resetDropTimestamps_Safe(int ch);
uint32_t getDropSequence_Safe(int ch);
unsigned long getLastDropTime_Safe(int ch);
long getPulseCount_Safe(int ch);
void handleEncoderInput();

#endif
