#pragma once
#include <Arduino.h>

// ==================================================
// CONFIGURATION
// ==================================================
const unsigned long MAX_DELAY_US = 5000;    // microseconds
const unsigned long MIN_DELAY_US = 50;      // microseconds
const float ARC_ANGLE_PER_POINT = 10.0;     // degrees
const int MOTOR_RAMP_RATIO = 4;  // rampSteps = totalSteps / MOTOR_RAMP_RATIO

// ==================================================
// Direction
// ==================================================
enum class Direction {
    NEGATIVE = -1,
    POSITIVE = 1
};

enum class ArcDirection {
    CLOCKWISE = 1,
    COUNTERCLOCKWISE = -1
};


enum class MotorStatus {
    IDEL = 0,
    ACCEL = 1,
    DECEL = 2,
    CONST = 3
};

unsigned long checkDelayUSLimit(unsigned long delayUS);

// ==================================================
// Motor Class
// ==================================================
class Motor {
private:
    int stepPin;
    int dirPin;
    int enaPin;

    int totalSteps;
    int currentStep;

    unsigned long lastStepUS;

    bool active;
    bool updated;
    bool isConti;

    MotorStatus status;
    Direction direction;

    unsigned long startDelayUS;
    unsigned long targetDelayUS;
    unsigned long currentDelayUS;

    unsigned long deltaDelayUS;

    void pulse();
    void updateSpeedProfile();

public:
    Motor(int stepPin, int dirPin, int enaPin = -1);

    void begin();
    void stop();
    void setMotion(Direction dir, int steps, unsigned long startDelayUS, unsigned long targetDelayUS, bool _isConti = false);
    void updateMotion();

    bool isMoving() const;
    bool isUpdated() const;

    int getTotalSteps() const;
    int getCurrentStep() const;
    int getRemainingSteps() const;
    unsigned long getCurrentDelayUS() const;
    MotorStatus getStatus() const;
};
