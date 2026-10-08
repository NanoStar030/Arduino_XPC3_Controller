#include "main_multi_motor_platform.h"
#include <math.h>

// ==================================================
// Global Variables
// ==================================================
Motor motors[MOTOR_COUNT] = {
    {11, 10},       // Motor 0 {step, dir}
    {13, 12},       // Motor 1
    {6, 7, 9},      // Motor 2 {step, dir, enable}
    {4, 5, 8}       // Motor 3
};

char lineBuf[LINE_BUF_SIZE];
int lineIndex = 0;

struct ArcMotion {
    int motor1Index;
    int motor2Index;

    int radiusSteps;
    float startAngle;
    float endAngle;
    float currentAngle;
    float remainingAngle;

    ArcDirection arcDirection;
    unsigned long targetDelayUS;

    bool active;
};

ArcMotion arcMotion;

// ==================================================
// Function Declarations
// ==================================================
void readSerial();
void handleSerial();

void handleLinearCommand(char *cmd);
void handleArcCommand(char *cmd);
void handleStopCommand(char *cmd);
void handleStatusCommand(char *cmd);

void setArcMotion(int motor1Index, int motor2Index, int radiusSteps,
                  float startAngle, float endAngle, ArcDirection arcDirection,
                  unsigned long targetDelayUS);
void updateArcMotion();
void updateMotors();

unsigned long calculateSyncDelay(int referenceSteps, unsigned long referenceDelayUS, int targetSteps);
float calculateArcTravel(float startAngle, float endAngle, ArcDirection direction);
float normalizeAngle(float angle);

// ==================================================
// Serial Functions
// ==================================================
void readSerial() {
    while (Serial.available() > 0) {
        char c = Serial.read();

        if (c == '\n') {
            if (lineIndex > 0) {
                lineBuf[lineIndex] = '\0';
                handleSerial();
                lineIndex = 0;
                lineBuf[0] = '\0';
            }
        }
        else if (c != '\r') {
            if (lineIndex < LINE_BUF_SIZE - 1) {
                lineBuf[lineIndex] = c;
                lineIndex++;
            }
            else {
                lineIndex = 0;
                lineBuf[0] = '\0';
                Serial.println("ERROR: COMMAND_TOO_LONG");
            }
        }
    }
}

void handleSerial() {
    if (strncmp(lineBuf, "LINEAR,", 7) == 0) handleLinearCommand(lineBuf);
    else if (strncmp(lineBuf, "ARC,", 4) == 0) handleArcCommand(lineBuf);
    else if (strcmp(lineBuf, "STOP") == 0) handleStopCommand(lineBuf);
    else if (strcmp(lineBuf, "STATUS") == 0) handleStatusCommand(lineBuf);
    else Serial.println("ERROR: UNKNOWN_COMMAND");
}

// ==================================================
// Command Handlers
// ==================================================
void handleLinearCommand(char *cmd) {
    // Parse two Motor commands
    int motor1Index;
    int direction1Value;
    int steps1;
    unsigned long delayUS1;
    int rampSteps1;

    int motor2Index;
    int direction2Value;
    int steps2;
    unsigned long delayUS2;
    int rampSteps2;

    int parsed = sscanf(
        cmd,
        "LINEAR,%d,%d,%d,%lu,%d,%d,%d,%d,%lu,%d",
        &motor1Index, &direction1Value, &steps1, &delayUS1, &rampSteps1,
        &motor2Index, &direction2Value, &steps2, &delayUS2, &rampSteps2
    );

    if (parsed != 10) {
        Serial.println("ERROR: INVALID_LINEAR_COMMAND");
        return;
    }

    if (motor1Index < 0 || motor1Index >= MOTOR_COUNT || motor2Index < 0 || motor2Index >= MOTOR_COUNT) {
        Serial.println("ERROR: INVALID_MOTOR_INDEX");
        return;
    }

    if ((direction1Value != -1 && direction1Value != 1) || (direction2Value != -1 && direction2Value != 1)) {
        Serial.println("ERROR: INVALID_DIRECTION");
        return;
    }

    if (steps1 <= 0 || steps2 <= 0) {
        Serial.println("ERROR: INVALID_STEPS");
        return;
    }

    Direction direction1 = static_cast<Direction>(direction1Value);
    Direction direction2 = static_cast<Direction>(direction2Value);

    // ==================================================
    // Single-axis Linear Motion
    // Motor 2 parameters are ignored when both indices are the same.
    // ==================================================
    if (motor1Index == motor2Index) {
        if (delayUS1 < MIN_DELAY_US || delayUS1 > MAX_DELAY_US || rampSteps1 < 0) {
            Serial.println("ERROR: INVALID_LINEAR_PARAMETER");
            return;
        }

        arcMotion.active = false;
        motors[motor1Index].setMotion(direction1, steps1, delayUS1, rampSteps1);
        return;
    }

    // All dual-axis linear motions use a 1/4-step ramp.
    rampSteps1 = steps1 / 4;
    rampSteps2 = steps2 / 4;

    // ==================================================
    // Asynchronous Dual-axis Linear Motion
    // ==================================================
    if (delayUS1 > 0 && delayUS2 > 0) {
        if (delayUS1 < MIN_DELAY_US || delayUS1 > MAX_DELAY_US || delayUS2 < MIN_DELAY_US || delayUS2 > MAX_DELAY_US) {
            Serial.println("ERROR: INVALID_LINEAR_DELAY");
            return;
        }

        arcMotion.active = false;
        motors[motor1Index].setMotion(direction1, steps1, delayUS1, rampSteps1);
        motors[motor2Index].setMotion(direction2, steps2, delayUS2, rampSteps2);
        return;
    }

    // ==================================================
    // Synchronous Dual-axis Linear Motion
    // Exactly one delay is supplied. The other delay is calculated so
    // both motors have approximately the same total motion time.
    // ==================================================
    if ((delayUS1 == 0) != (delayUS2 == 0)) {
        if (delayUS1 > 0) {
            if (delayUS1 < MIN_DELAY_US || delayUS1 > MAX_DELAY_US) {
                Serial.println("ERROR: INVALID_LINEAR_DELAY");
                return;
            }
            delayUS2 = calculateSyncDelay(steps1, delayUS1, steps2);
        }
        else {
            if (delayUS2 < MIN_DELAY_US || delayUS2 > MAX_DELAY_US) {
                Serial.println("ERROR: INVALID_LINEAR_DELAY");
                return;
            }
            delayUS1 = calculateSyncDelay(steps2, delayUS2, steps1);
        }

        if (delayUS1 < MIN_DELAY_US || delayUS1 > MAX_DELAY_US || delayUS2 < MIN_DELAY_US || delayUS2 > MAX_DELAY_US) {
            Serial.println("ERROR: SYNC_SPEED_OUT_OF_RANGE");
            return;
        }

        arcMotion.active = false;
        motors[motor1Index].setMotion(direction1, steps1, delayUS1, rampSteps1);
        motors[motor2Index].setMotion(direction2, steps2, delayUS2, rampSteps2);
        return;
    }

    Serial.println("ERROR: INVALID_LINEAR_DELAY");
}

void handleArcCommand(char *cmd) {
    int motor1Index;
    int motor2Index;
    int radiusSteps;
    float startAngle;
    float endAngle;
    int arcDirectionValue;
    unsigned long delayUS;

    int parsed = sscanf(
        cmd,
        "ARC,%d,%d,%d,%f,%f,%d,%lu",
        &motor1Index, &motor2Index, &radiusSteps,
        &startAngle, &endAngle, &arcDirectionValue, &delayUS
    );

    if (parsed != 7) {
        Serial.println("ERROR: INVALID_ARC_COMMAND");
        return;
    }

    if (motor1Index < 0 || motor1Index >= MOTOR_COUNT || motor2Index < 0 || motor2Index >= MOTOR_COUNT) {
        Serial.println("ERROR: INVALID_MOTOR_INDEX");
        return;
    }

    if (motor1Index == motor2Index) {
        Serial.println("ERROR: ARC_REQUIRES_TWO_MOTORS");
        return;
    }

    if (arcDirectionValue != -1 && arcDirectionValue != 1) {
        Serial.println("ERROR: INVALID_ARC_DIRECTION");
        return;
    }

    if (radiusSteps <= 0) {
        Serial.println("ERROR: INVALID_RADIUS");
        return;
    }

    if (delayUS < MIN_DELAY_US || delayUS > MAX_DELAY_US) {
        Serial.println("ERROR: INVALID_ARC_DELAY");
        return;
    }

    ArcDirection arcDirection = static_cast<ArcDirection>(arcDirectionValue);

    for (int i = 0; i < MOTOR_COUNT; i++) motors[i].stop();

    setArcMotion(motor1Index, motor2Index, radiusSteps, startAngle, endAngle, arcDirection, delayUS);
}

void handleStopCommand(char *cmd) {
    (void)cmd;

    arcMotion.active = false;
    for (int i = 0; i < MOTOR_COUNT; i++) motors[i].stop();

    Serial.println("STOPPED");
}

void handleStatusCommand(char *cmd) {
    (void)cmd;

    Serial.print("STATUS:");

    for (int i = 0; i < MOTOR_COUNT; i++) {
        Serial.print(" M");
        Serial.print(i);
        Serial.print("=");
        Serial.print(motors[i].isMoving() ? "MOVING" : "IDLE");
    }

    Serial.print(" ARC=");
    Serial.println(arcMotion.active ? "ACTIVE" : "IDLE");
}

// ==================================================
// Motion Helpers
// ==================================================
unsigned long calculateSyncDelay(int referenceSteps, unsigned long referenceDelayUS, int targetSteps) {
    if (referenceSteps <= 0 || targetSteps <= 0) return 0;

    // With rampSteps = totalSteps / 4, approximately half of the motion
    // is ramping and half is at target speed. The mean delay is therefore:
    // averageDelay ~= 0.25 * START_DELAY_US + 0.75 * targetDelayUS
    double referenceAverageDelay = 0.25 * START_DELAY_US + 0.75 * referenceDelayUS;
    double totalTime = referenceSteps * referenceAverageDelay;
    double targetAverageDelay = totalTime / targetSteps;
    double targetDelay = (targetAverageDelay - 0.25 * START_DELAY_US) / 0.75;

    if (targetDelay < 0.0) return 0;
    return (unsigned long)round(targetDelay);
}

float normalizeAngle(float angle) {
    while (angle >= 360.0) angle -= 360.0;
    while (angle < 0.0) angle += 360.0;
    return angle;
}

float calculateArcTravel(float startAngle, float endAngle, ArcDirection direction) {
    startAngle = normalizeAngle(startAngle);
    endAngle = normalizeAngle(endAngle);

    if (direction == ArcDirection::CLOCKWISE) {
        float travel = startAngle - endAngle;
        if (travel < 0.0) travel += 360.0;
        return travel;
    }

    float travel = endAngle - startAngle;
    if (travel < 0.0) travel += 360.0;
    return travel;
}

// ==================================================
// Arc Motion Controller
// ==================================================
void setArcMotion(int motor1Index, int motor2Index, int radiusSteps,
                  float startAngle, float endAngle, ArcDirection arcDirection,
                  unsigned long targetDelayUS) {

    arcMotion.motor1Index = motor1Index;
    arcMotion.motor2Index = motor2Index;
    arcMotion.radiusSteps = radiusSteps;

    arcMotion.startAngle = normalizeAngle(startAngle);
    arcMotion.endAngle = normalizeAngle(endAngle);
    arcMotion.currentAngle = arcMotion.startAngle;
    arcMotion.remainingAngle = calculateArcTravel(arcMotion.startAngle, arcMotion.endAngle, arcDirection);

    arcMotion.arcDirection = arcDirection;
    arcMotion.targetDelayUS = targetDelayUS;
    arcMotion.active = arcMotion.remainingAngle > 0.0001;
}

void updateArcMotion() {
    if (!arcMotion.active) return;

    // Do not calculate the next point until the current segment is finished.
    if (motors[arcMotion.motor1Index].isMoving() || motors[arcMotion.motor2Index].isMoving()) return;

    if (arcMotion.remainingAngle <= 0.0001) {
        arcMotion.active = false;
        return;
    }

    float segmentAngle = min(ARC_ANGLE_PER_POINT, arcMotion.remainingAngle);
    float directionSign = arcMotion.arcDirection == ArcDirection::CLOCKWISE ? -1.0 : 1.0;
    float nextAngle = normalizeAngle(arcMotion.currentAngle + directionSign * segmentAngle);

    float currentRad = arcMotion.currentAngle * PI / 180.0;
    float nextRad = nextAngle * PI / 180.0;

    int currentX = (int)round(arcMotion.radiusSteps * cos(currentRad));
    int currentY = (int)round(arcMotion.radiusSteps * sin(currentRad));
    int nextX = (int)round(arcMotion.radiusSteps * cos(nextRad));
    int nextY = (int)round(arcMotion.radiusSteps * sin(nextRad));

    int deltaSteps1 = nextX - currentX;
    int deltaSteps2 = nextY - currentY;

    int steps1 = abs(deltaSteps1);
    int steps2 = abs(deltaSteps2);

    Direction direction1 = deltaSteps1 >= 0 ? Direction::POSITIVE : Direction::NEGATIVE;
    Direction direction2 = deltaSteps2 >= 0 ? Direction::POSITIVE : Direction::NEGATIVE;

    bool firstSegment = fabs(arcMotion.remainingAngle - calculateArcTravel(arcMotion.startAngle, arcMotion.endAngle, arcMotion.arcDirection)) < 0.0001;
    bool lastSegment = arcMotion.remainingAngle <= ARC_ANGLE_PER_POINT + 0.0001;

    // ARC ramp is controlled segment-by-segment here. Motor rampSteps stays 0.
    unsigned long segmentDelayUS = arcMotion.targetDelayUS;
    if (firstSegment || lastSegment) segmentDelayUS = START_DELAY_US;

    if (steps1 > 0 && steps2 > 0) {
        unsigned long delayUS1;
        unsigned long delayUS2;

        // ARC motors have rampSteps = 0, so synchronization is based on
        // steps * delay.
        unsigned long majorSteps = max(steps1, steps2);
        if (steps1 == majorSteps) {
            delayUS1 = segmentDelayUS;
            delayUS2 = (unsigned long)round((double)steps1 * delayUS1 / steps2);
        }
        else {
            delayUS2 = segmentDelayUS;
            delayUS1 = (unsigned long)round((double)steps2 * delayUS2 / steps1);
        }

        if (delayUS1 < MIN_DELAY_US) delayUS1 = MIN_DELAY_US;
        if (delayUS1 > MAX_DELAY_US) delayUS1 = MAX_DELAY_US;
        if (delayUS2 < MIN_DELAY_US) delayUS2 = MIN_DELAY_US;
        if (delayUS2 > MAX_DELAY_US) delayUS2 = MAX_DELAY_US;

        motors[arcMotion.motor1Index].setMotion(direction1, steps1, delayUS1, 0);
        motors[arcMotion.motor2Index].setMotion(direction2, steps2, delayUS2, 0);
    }
    else if (steps1 > 0) {
        motors[arcMotion.motor1Index].setMotion(direction1, steps1, segmentDelayUS, 0);
    }
    else if (steps2 > 0) {
        motors[arcMotion.motor2Index].setMotion(direction2, steps2, segmentDelayUS, 0);
    }

    arcMotion.currentAngle = nextAngle;
    arcMotion.remainingAngle -= segmentAngle;

    if (arcMotion.remainingAngle < 0.0001) arcMotion.remainingAngle = 0.0;
}

// ==================================================
// Motor Update
// ==================================================
void updateMotors() {
    for (int i = 0; i < MOTOR_COUNT; i++) motors[i].updateMotion();
}

// ==================================================
// Arduino Setup
// ==================================================
void setup() {
    Serial.begin(115200);

    for (int i = 0; i < MOTOR_COUNT; i++) motors[i].begin();

    arcMotion.active = false;
    lineBuf[0] = '\0';

    Serial.println("READY");
}

// ==================================================
// Arduino Main Loop
// ==================================================
void loop() {
    readSerial();
    updateArcMotion();
    updateMotors();
}
