#include "Motor.h"

// ==================================================
// Constructor
// ==================================================
Motor::Motor(int stepPin, int dirPin, int enaPin) {
    this->stepPin = stepPin;
    this->dirPin = dirPin;
    this->enaPin = enaPin;

    totalSteps = 0;
    currentStep = 0;
    lastStepUS = 0;

    direction = Direction::POSITIVE;
    active = false;

    rampSteps = 0;

    startDelayUS = START_DELAY_US;
    targetDelayUS = 0;
    currentDelayUS = 0;
    deltaDelayUS = 0;
}

// ==================================================
// Begin
// ==================================================
void Motor::begin() {
    pinMode(stepPin, OUTPUT);
    pinMode(dirPin, OUTPUT);

    digitalWrite(stepPin, LOW);
    digitalWrite(dirPin, LOW);

    if (enaPin >= 0) {
        pinMode(enaPin, OUTPUT);
        digitalWrite(enaPin, HIGH); // Disable motor by default
    }
}

// ==================================================
// Start Motion
// ==================================================
void Motor::setMotion(Direction dir, int steps, unsigned long delayUS, int rampSteps) {
    if (steps <= 0 || delayUS == 0) return;

    if (delayUS < MIN_DELAY_US) delayUS = MIN_DELAY_US;
    if (delayUS > MAX_DELAY_US) delayUS = MAX_DELAY_US;

    direction = dir;
    totalSteps = steps;
    currentStep = 0;
    targetDelayUS = delayUS;
    this->rampSteps = rampSteps;

    digitalWrite(dirPin, direction == Direction::POSITIVE ? HIGH : LOW);

    if (enaPin >= 0) digitalWrite(enaPin, LOW);

    if (this->rampSteps > 0 && targetDelayUS < startDelayUS) {
        if (this->rampSteps > totalSteps / 4) this->rampSteps = totalSteps / 4;

        if (this->rampSteps > 0) {
            deltaDelayUS = (startDelayUS - targetDelayUS) / this->rampSteps;
            if (deltaDelayUS < 1) deltaDelayUS = 1;
            currentDelayUS = startDelayUS;
        }
        else {
            currentDelayUS = targetDelayUS;
            deltaDelayUS = 0;
        }
    }
    else {
        this->rampSteps = 0;
        currentDelayUS = targetDelayUS;
        deltaDelayUS = 0;
    }

    lastStepUS = micros();
    active = true;
}

// ==================================================
// Update
// ==================================================
void Motor::updateMotion() {
    if (!active) return;

    unsigned long now = micros();
    if (now - lastStepUS < currentDelayUS) return;

    lastStepUS = now;

    pulse();
    currentStep++;

    if (currentStep >= totalSteps) {
        stop();
        return;
    }

    updateSpeedProfile();
}

// ==================================================
// STEP Pulse
// ==================================================
void Motor::pulse() {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(stepPin, LOW);
}

// ==================================================
// Speed Profile
// ==================================================
void Motor::updateSpeedProfile() {
    if (rampSteps <= 0 || deltaDelayUS == 0) {
        currentDelayUS = targetDelayUS;
        return;
    }

    if (currentStep < rampSteps) {
        if (currentDelayUS > targetDelayUS + deltaDelayUS) currentDelayUS -= deltaDelayUS;
        else currentDelayUS = targetDelayUS;
    }
    else if (currentStep >= totalSteps - rampSteps) {
        currentDelayUS += deltaDelayUS;
        if (currentDelayUS > startDelayUS) currentDelayUS = startDelayUS;
    }
    else {
        currentDelayUS = targetDelayUS;
    }
}

// ==================================================
// Stop
// ==================================================
void Motor::stop() {
    active = false;
    digitalWrite(stepPin, LOW);
    if (enaPin >= 0) digitalWrite(enaPin, HIGH);
}

// ==================================================
// Status
// ==================================================
bool Motor::isMoving() const {
    return active;
}

int Motor::getCurrentStep() const {
    return currentStep;
}

int Motor::getTotalSteps() const {
    return totalSteps;
}

int Motor::getRemainingSteps() const {
    return totalSteps - currentStep;
}
