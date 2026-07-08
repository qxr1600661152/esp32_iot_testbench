#pragma once

#include <Arduino.h>

void epdPowerBegin();

void epdPowerIdle(uint32_t ms, bool allowLightSleep);
