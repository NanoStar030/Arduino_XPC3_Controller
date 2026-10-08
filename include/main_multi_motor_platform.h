#pragma once
#include <Arduino.h>
#include "Motor.h"

// ==================================================
// System Configuration
// ==================================================
constexpr int MOTOR_COUNT = 4;
constexpr int LINE_BUF_SIZE = 128;

// ==================================================
// Global Variables
// ==================================================
extern char lineBuf[LINE_BUF_SIZE];
extern int lineIndex;
extern Motor motors[MOTOR_COUNT];
