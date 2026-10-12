#include "Motor.h"

unsigned long checkDelayUSLimit(unsigned long _delayUS) {
    unsigned long updatedDelayUS = _delayUS;
    if (_delayUS < MIN_DELAY_US) updatedDelayUS = MIN_DELAY_US;
    if (_delayUS > MAX_DELAY_US) updatedDelayUS = MAX_DELAY_US;
    return updatedDelayUS;
}

// Constructor
Motor::Motor(int stepPin, int dirPin, int enaPin) {
    this->stepPin = stepPin;
    this->dirPin = dirPin;
    this->enaPin = enaPin;

    totalSteps = 0;
    currentStep = 0;
    lastStepUS = 0;

    direction = Direction::POSITIVE;
    active = false;
    updated = false;
    isConti = false;
    status = MotorStatus::IDEL;
    startDelayUS = MAX_DELAY_US;
    targetDelayUS = MAX_DELAY_US;
    currentDelayUS = MAX_DELAY_US;
    deltaDelayUS = 0;
}

// Begin
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

// Start Motion
void Motor::setMotion(Direction dir, int steps, unsigned long _startDelayUS, unsigned long _targetDelayUS, bool _isConti) {
    if (steps <= 0 || _targetDelayUS == 0) return;
    
    isConti = _isConti;
    direction = dir;
    totalSteps = steps;
    currentStep = 0;
    
    // Adjust delayUS if it exceeds Maximum or Minimum limit
    startDelayUS = checkDelayUSLimit(_startDelayUS);
    targetDelayUS = checkDelayUSLimit(_targetDelayUS);
    
    // Set Motor status and delta delay for ACCEL and DECEL
    if (startDelayUS > targetDelayUS){
        status = MotorStatus::ACCEL;
        deltaDelayUS = (startDelayUS - targetDelayUS) / (totalSteps / MOTOR_RAMP_RATIO); // Delta Delay for ACCEL
    }
    else if(startDelayUS == targetDelayUS) {
        status = MotorStatus::CONST;
        deltaDelayUS = 0;
    }
    else {
        status = MotorStatus::DECEL;
        deltaDelayUS = (targetDelayUS - startDelayUS) / (totalSteps / MOTOR_RAMP_RATIO); // Delta Delay for DECEL
    }

    // Direction
    digitalWrite(dirPin, direction == Direction::POSITIVE ? HIGH : LOW);

    // Current Delay Initialization
    currentDelayUS = startDelayUS;

    // Timer Initialization
    lastStepUS = micros();

    // Enable
    if (enaPin >= 0) digitalWrite(enaPin, LOW);
    active = true;
}

// Update
void Motor::updateMotion() {
    if (!active) return;

    unsigned long now = micros(); // Time now
    if (now - lastStepUS < currentDelayUS) { // Elapsed time
        updated = false;
        return;
    }
    updated = true;
    lastStepUS = now;
    pulse();
    currentStep++;

    if (currentStep >= totalSteps) {
        stop();
        return;
    }

    updateSpeedProfile();
}

// STEP Pulse
void Motor::pulse() {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(stepPin, LOW);
}

// Speed Profile
void Motor::updateSpeedProfile() {
    if (status == MotorStatus::ACCEL) {
        if (currentStep < (totalSteps / MOTOR_RAMP_RATIO)) {
            currentDelayUS -= deltaDelayUS;
            currentDelayUS = checkDelayUSLimit(currentDelayUS);
        }
    }
    else if (status == MotorStatus::DECEL) {
        if (currentStep >= (totalSteps - (totalSteps / MOTOR_RAMP_RATIO))) {
            currentDelayUS += deltaDelayUS;
            currentDelayUS = checkDelayUSLimit(currentDelayUS);
        }
    }
}

// ==================================================
// Stop
// ==================================================
void Motor::stop() {
    active = false; // Finish the movement of this setting
    updated = false;

    if (!isConti) { // If the motor has next movement, keep the status, direction and enable.
        status = MotorStatus::IDEL;
        digitalWrite(stepPin, LOW);
        if (enaPin >= 0) digitalWrite(enaPin, HIGH);
    }
}

// ==================================================
// Status
// ==================================================

bool Motor::isMoving() const {
    return active;
}

bool Motor::isUpdated() const {
    return updated;
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

unsigned long Motor::getCurrentDelayUS() const {
    return currentDelayUS;
}

MotorStatus Motor::getStatus() const {
    return status;
}