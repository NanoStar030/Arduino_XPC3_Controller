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
enum class ArcDirection
{
    CW = 0,
    CCW = 1
};

//========================
// Move Point Status
enum class PointStatus {
    ACCELERATING,
    CONSTANT_SPEED,
    DECELERATING,
    FULL_RANGE
};

//========================
// Move Command Definition
struct AxisMoveCommand {
    int motorIdx;
    Direction dir;       // POSITIVE or NEGATIVE.
    int stepNUM;      // Number of steps to move.
    int stepDelayUS;   // Time interval between steps in microseconds.
    int rampTicks;  // Number of steps for acceleration and deceleration.
};

struct DualMoveCommand {
    AxisMoveCommand motorA;
    AxisMoveCommand motorB;
};

struct AxisMotionRuntime {
    int stepTicks;
    int rampTicks;
    int ticksIndex;

    unsigned long lastStepUS;      // the time of the last step in microseconds
    unsigned long deltaDelayUS;    // the change in delay per step during acceleration and deceleration
    unsigned long targetDelayUS;   // target delay between steps in microseconds
    unsigned long currentDelayUS;  // current delay between steps in microseconds, it will change during acceleration and deceleration
};

struct DualMotionRuntime {
    AxisMotionRuntime motorA;
    AxisMotionRuntime motorB;
    PointStatus pointStatus;  // Current status of the motion (accelerating, full speed, or decelerating)
};

struct ArcMoveCommand
{
    int motorA;
    int motorB;

    long radiusStepsA;
    long radiusStepsB;

    int angleDeg;

    ArcDirection dir;

    // Desired time for each 1-degree segment.
    // Python GUI can convert speed -> segmentTimeUS.
    unsigned long segmentTimeUS;
};

struct ArcMotionRuntime
{
    int totalSegments;
    int segmentIndex;

    long previousStepA;
    long previousStepB;

    bool active;
};


// ==========================================
// ARC SETTINGS
// ==========================================

const int ARC_DEG_PER_POINT = 1;

// Number of 1-degree segments used for
// acceleration / deceleration
const int ARC_RAMP_SEGMENTS = 3;

// Starting delay = target delay × this value
// 2.0 means starting at about half speed
const float ARC_START_DELAY_SCALE = 2.0f;

//========================
// System State
enum class SystemState {
    IDLE,
    S_RUNNING,
    D_RUNNING,
    ARC_RUNNING,
    ERROR,
};

//========================
// Motor Configs
constexpr int MOTOR_COUNT = 4;
extern Motor motors[MOTOR_COUNT];

//========================
// Motor Parameters
// constexpr int STEPS_PER_MM = 100.0f;
constexpr int MAX_DELAY_US = 5000;
constexpr int MIN_DELAY_US = 50;
constexpr int START_DELAY_US = 1000;

//========================
// Serial Configs
constexpr int LINE_BUF_SIZE = 64;

//========================
// Global State Declarations
extern char lineBuf[LINE_BUF_SIZE];
extern int lineIndex;
extern SystemState systemState;
extern AxisMoveCommand currentMove_A;
extern DualMoveCommand   currentMove_D;
extern AxisMotionRuntime Motion_A;
extern DualMotionRuntime   Motion_D;