// main.h
#pragma once

#include <Arduino.h>

// ==================================================
// MOTOR DEFINITION
// ==================================================

struct Motor
{
    int stepPin;
    int dirPin;
    int enaPin;
};


// ==================================================
// DIRECTION
// ==================================================

enum class Direction
{
    NEGATIVE = 0,
    POSITIVE = 1
};

enum class ArcDirection
{
    CW = 0,
    CCW = 1
};


// ==================================================
// MOVE POINT STATUS
// ==================================================

enum class PointStatus
{
    ACCELERATING,
    CONSTANT_SPEED,
    DECELERATING,
    FULL_RANGE
};


// ==================================================
// SINGLE AXIS MOVE COMMAND
// ==================================================

struct AxisMoveCommand
{
    int motorIdx;

    Direction dir;

    long stepNUM;

    unsigned long stepDelayUS;

    long rampTicks;
};


// ==================================================
// DUAL AXIS MOVE COMMAND
// ==================================================

struct DualMoveCommand
{
    AxisMoveCommand motorA;
    AxisMoveCommand motorB;
};


// ==================================================
// SINGLE AXIS MOTION RUNTIME
// ==================================================

struct AxisMotionRuntime
{
    long stepTicks;
    long rampTicks;
    long ticksIndex;

    unsigned long lastStepUS;

    unsigned long deltaDelayUS;
    unsigned long targetDelayUS;
    unsigned long currentDelayUS;
};


// ==================================================
// DUAL AXIS MOTION RUNTIME
// ==================================================

struct DualMotionRuntime
{
    AxisMotionRuntime motorA;
    AxisMotionRuntime motorB;

    PointStatus pointStatus;
};


// ==================================================
// ARC MOVE COMMAND
// ==================================================

struct ArcMoveCommand
{
    // Two motors used to form the ARC
    int motorA;
    int motorB;

    // Radius already converted to steps by Python.
    // A and B can be different because the two axes
    // may have different steps/mm.
    long radiusStepsA;
    long radiusStepsB;

    // Total ARC angle
    int angleDeg;

    // CW or CCW
    ArcDirection dir;

    // Desired time for each ARC segment
    unsigned long segmentTimeUS;
};


// ==================================================
// ARC MOTION RUNTIME
// ==================================================

struct ArcMotionRuntime
{
    // Total number of ARC segments
    int totalSegments;

    // Current segment index
    int segmentIndex;

    // Previous absolute ARC position
    long previousStepA;
    long previousStepB;

    // Whether ARC motion is currently active
    bool active;
};


// ==================================================
// SYSTEM STATE
// ==================================================

enum class SystemState
{
    IDLE,

    S_RUNNING,
    D_RUNNING,
    ARC_RUNNING,

    ERROR
};


// ==================================================
// MOTOR CONFIGURATION
// ==================================================

constexpr int MOTOR_COUNT = 4;

extern Motor motors[MOTOR_COUNT];


// ==================================================
// MOTOR PARAMETERS
// ==================================================

// Physical conversion such as steps/mm is handled
// by the Python GUI, not Arduino.

constexpr unsigned long MAX_DELAY_US   = 5000;
constexpr unsigned long MIN_DELAY_US   = 50;
constexpr unsigned long START_DELAY_US = 1000;


// ==================================================
// SERIAL CONFIGURATION
// ==================================================

constexpr int LINE_BUF_SIZE = 64;


// ==================================================
// GLOBAL SYSTEM STATE
// ==================================================

extern char lineBuf[LINE_BUF_SIZE];
extern int lineIndex;

extern SystemState systemState;


// ==================================================
// SINGLE MOTION GLOBALS
// ==================================================

extern AxisMoveCommand currentMove_A;
extern AxisMotionRuntime Motion_A;


// ==================================================
// DUAL MOTION GLOBALS
// ==================================================

extern DualMoveCommand currentMove_D;
extern DualMotionRuntime Motion_D;


// ==================================================
// ARC MOTION GLOBALS
// ==================================================

extern ArcMoveCommand currentMove_Arc;
extern ArcMotionRuntime Motion_Arc;