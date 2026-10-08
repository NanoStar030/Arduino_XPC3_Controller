#pragma once
#include <Arduino.h>

// ==================================================
// CONFIGURATION
// ==================================================
const unsigned long START_DELAY_US = 5000;  // microseconds
const unsigned long MAX_DELAY_US = 5000;    // microseconds
const unsigned long MIN_DELAY_US = 50;      // microseconds
const float ARC_ANGLE_PER_POINT = 10.0;     // degrees

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

    Direction direction;
    bool active;

    int rampSteps;

    unsigned long startDelayUS;
    unsigned long targetDelayUS;
    unsigned long currentDelayUS;
    unsigned long deltaDelayUS;

    void pulse();
    void updateSpeedProfile();

public:
    Motor(int stepPin, int dirPin, int enaPin = -1);

    void begin();
    void setMotion(Direction dir, int steps, unsigned long delayUS, int rampSteps = 0);
    void updateMotion();
    void stop();

    bool isMoving() const;
    int getCurrentStep() const;
    int getTotalSteps() const;
    int getRemainingSteps() const;
};
