// mian.h
#pragma once
#include <Arduino.h>

//========================
// Motor Definition
struct Motor {
    int stepPin;
    int dirPin;
    int enaPin;
};

//========================
// Direction
enum class Direction {
    NEGATIVE,
    POSITIVE,
};

//========================
// Move Command Definition
struct SingleMoveCommand {
    int motorIdx;
    Direction dir;       // POSITIVE or NEGATIVE.
    int stepNUM;      // Number of steps to move.
    int stepDelayUS;   // Time interval between steps in microseconds.
    int rampStepNUM;  // Number of steps for acceleration and deceleration.
};

struct DualMoveCommand {
    int motorA;
    int motorB;

    long stepsA;
    long stepsB;

    unsigned long stepDelayUS;
    long rampStepNUM;
};

struct SingleMotionRuntime {
    int stepNUM;
    int rampStepNUM;
    int stepIdx;
    unsigned long startDelay;
    unsigned long minDelay;
    unsigned long currentDelay;
    unsigned long delta;
    unsigned long lastStepUS;
};

struct DualMotionRuntime {
    long totalStepsA;
    long totalStepsB;

    long totalTicks;
    long tickIndex;

    long accumulatorA;
    long accumulatorB;

    long rampTicks;

    unsigned long lastStepUS;
    unsigned long startDelayUS;
    unsigned long minDelayUS;
    unsigned long currentDelayUS;
    unsigned long deltaDelayUS;
};

//========================
// System State
enum class SystemState {
    IDLE,
    S_RUNNING,
    D_RUNNING,
    ERROR,
};

//========================
// Motor Configs
constexpr int MOTOR_COUNT = 4;
extern Motor motors[MOTOR_COUNT];

//========================
// Motor Parameters
// constexpr int STEPS_PER_MM = 100.0f;
constexpr int MAX_DELAY_US = 500;
constexpr int MIN_DELAY_US = 10;
constexpr int START_DELAY_US = 250;

//========================
// Serial Configs
constexpr int LINE_BUF_SIZE = 64;

//========================
// Global State Declarations
extern char lineBuf[LINE_BUF_SIZE];
extern int lineIndex;
extern SystemState systemState;
extern SingleMoveCommand current_s_Move;
extern DualMoveCommand   current_d_Move;
extern SingleMotionRuntime s_Motion;
extern DualMotionRuntime   d_Motion;