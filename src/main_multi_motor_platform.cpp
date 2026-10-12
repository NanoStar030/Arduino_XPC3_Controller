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
bool isMotorDebug = true;
bool isArcMotionDebug = true;

struct LinearMotion {
    int motor1Index;
    int motor2Index;
    int steps1;
    int steps2;
    unsigned long delay1;
    unsigned long delay2;
    Direction direction1;
    Direction direction2;
    bool active1;
    bool active2;
};
LinearMotion linearMotion;

struct ArcMotion {
// These will be determinded in the setArcMotion()
    int motor1Index;
    int motor2Index;

    int radiusSteps;
    int startAngle;
    int endAngle;

    int totalSegment; 
    int currentSegment;

    ArcDirection arcDirection;
    unsigned long targetDelayUS;

    int deltaX[36];
    int deltaY[36];

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
                  int startAngle, int endAngle, ArcDirection arcDirection, unsigned long targetDelayUS);
void updateArcMotion();
void setLinearMotion(int motor1Index, int motor2Index, Direction direction1, Direction direction2, int steps1, int steps2, unsigned long dalay1, unsigned long delay2);
void updateLinearMotion();
void updateMotors();

unsigned long calculateSyncDelay(int referenceSteps, unsigned long referenceDelayUS, int targetSteps, bool isAccel = true);
int calculateArcTravel(int startAngle, int endAngle, ArcDirection direction);
int normalizeAngle(int angle);

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
        setLinearMotion(motor1Index, motor2Index, direction1, direction2, steps1/2, steps2/2, delayUS1, delayUS2);
        arcMotion.active = false;
        motors[motor1Index].setMotion(direction1, steps1/2, MAX_DELAY_US, delayUS1, true);
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
        setLinearMotion(motor1Index, motor2Index, direction1, direction2, steps1/2, steps2/2, delayUS1, delayUS2);
        arcMotion.active = false;
        motors[motor1Index].setMotion(direction1, steps1/2, MAX_DELAY_US, delayUS1, true);
        motors[motor2Index].setMotion(direction2, steps2/2, MAX_DELAY_US, delayUS2, true);
        return;
    }

    // ==================================================
    // Synchronous Dual-axis Linear Motion
    // Exactly one delay is supplied. The other delay is calculated so
    // both motors have approximately the same total motion time.
    // ==================================================
    if ((delayUS1 == 0) != (delayUS2 == 0)) {
        unsigned long delay = 0;
        if (delayUS1 != 0) {
            delay = delayUS1;
        }
        else {
            delay = delayUS2;
        }
        if (steps1 > steps2){ // motor1 needs faster speed (smaller delay)
            delayUS1 = delay;
            delayUS2 = calculateSyncDelay(steps1, delayUS1, steps2); // delay2 > delay1
        }
        else { // motor2 needs faster speed (smaller delay)
            delayUS2 = delay;
            delayUS1 = calculateSyncDelay(steps2, delayUS2, steps1); // delay1 > delay2
        }
        if (delayUS1 < MIN_DELAY_US || delayUS1 > MAX_DELAY_US || delayUS2 < MIN_DELAY_US || delayUS2 > MAX_DELAY_US) {
            Serial.println("ERROR: SYNC_SPEED_OUT_OF_RANGE");
            return;
        }
        setLinearMotion(motor1Index, motor2Index, direction1, direction2, steps1/2, steps2/2, delayUS1, delayUS2);
        arcMotion.active = false;
        motors[motor1Index].setMotion(direction1, steps1, MAX_DELAY_US, delayUS1, true);
        motors[motor2Index].setMotion(direction2, steps2, MAX_DELAY_US, delayUS2, true);
        return;
    }

    Serial.println("ERROR: INVALID_LINEAR_DELAY");
}

void handleArcCommand(char *cmd) {
    int motor1Index;
    int motor2Index;
    int radiusSteps;
    int startAngle;
    int endAngle;
    int arcDirectionValue;
    unsigned long delayUS;

    int parsed = sscanf(
        cmd,
        "ARC,%d,%d,%d,%d,%d,%d,%lu",
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
unsigned long calculateSyncDelay(int referenceSteps, unsigned long referenceDelayUS, int targetSteps, bool isAccel) {
    if (referenceSteps <= 0 || targetSteps <= 0) return 0;
    
    double referenceAverageDelay = 0;
    double totalTime = 0;
    double targetAverageDelay = 0;
    double targetDelay = 0;    

    // With rampSteps = totalSteps / 4, approximately half of the motion
    // is ramping and half is at target speed. The mean delay is therefore:
    // averageDelay ~= 0.25 * START_DELAY_US + 0.75 * targetDelayUS
    if (isAccel) {
        referenceAverageDelay = 0.25 * MAX_DELAY_US + 0.75 * referenceDelayUS;
        totalTime = referenceSteps * referenceAverageDelay;
        targetAverageDelay = totalTime / targetSteps;
        targetDelay = (targetAverageDelay - 0.25 * MAX_DELAY_US) / 0.75;    
    }
    else {
        totalTime = referenceSteps * referenceDelayUS;
        targetDelay = totalTime / targetSteps; 
    }
    targetDelay = checkDelayUSLimit(targetDelay);
    return (unsigned long)round(targetDelay);
}

int normalizeAngle(int angle) { // Make sure the angle is between 0 and 360
    while (angle >= 360) angle -= 360;
    while (angle < 0) angle += 360;
    return angle;
}

int calculateArcTravel(int startAngle, int endAngle, ArcDirection direction) {
    startAngle = normalizeAngle(startAngle);
    endAngle = normalizeAngle(endAngle);
    int travel = 0;
    if (direction == ArcDirection::CLOCKWISE) {
        travel = startAngle - endAngle;
    }
    else {
        travel = endAngle - startAngle;
    }
    if (travel < 0) travel += 360;
    return travel;
}

void setLinearMotion(int motor1Index, int motor2Index, Direction direction1, Direction direction2, int steps1, int steps2, unsigned long delay1, unsigned long delay2){
    linearMotion.motor1Index = motor1Index;
    linearMotion.motor2Index = motor2Index;
    linearMotion.direction1 = direction1;
    linearMotion.direction2 = direction2;
    linearMotion.delay1 = delay1;
    linearMotion.delay2 = delay2;
    linearMotion.steps1 = steps1;
    linearMotion.steps2 = steps2;
    linearMotion.active1 = true;
    if (motor1Index == motor2Index) {
        linearMotion.active2 = false;
    }
    else {
        linearMotion.active2 = true;
    }
}

void updateLinearMotion() {
    if (!linearMotion.active1 && !linearMotion.active2) return;
    if (!motors[linearMotion.motor1Index].isMoving()){ // Motor1 finished the movement
        if (motors[linearMotion.motor1Index].getStatus() == MotorStatus::ACCEL){
            motors[linearMotion.motor1Index].setMotion(linearMotion.direction1, linearMotion.steps1, linearMotion.delay1, MAX_DELAY_US, false);
        }
        else {
            linearMotion.active1 = false;
        }
    }
    if (!motors[linearMotion.motor2Index].isMoving()){ // Motor2 finished the movement
        if (motors[linearMotion.motor2Index].getStatus() == MotorStatus::ACCEL){
            motors[linearMotion.motor2Index].setMotion(linearMotion.direction2, linearMotion.steps2, linearMotion.delay2, MAX_DELAY_US, false);
        }
        else {
            linearMotion.active2 = false;
        }
    }
}

// ==================================================
// Arc Motion Controller
// ==================================================
void setArcMotion(int motor1Index, int motor2Index, int radiusSteps, int startAngle, int endAngle, 
                                                        ArcDirection arcDirection, unsigned long targetDelayUS) {

    arcMotion.motor1Index = motor1Index;
    arcMotion.motor2Index = motor2Index;
    arcMotion.radiusSteps = radiusSteps;

    arcMotion.startAngle = normalizeAngle(startAngle);
    arcMotion.endAngle = normalizeAngle(endAngle);
    
    arcMotion.currentSegment = 0;
    arcMotion.totalSegment = abs(arcMotion.startAngle - arcMotion.endAngle) / ARC_ANGLE_PER_POINT;

    arcMotion.arcDirection = arcDirection;
    arcMotion.targetDelayUS = targetDelayUS;
    arcMotion.active = arcMotion.totalSegment > 0;

    int directionSign = arcMotion.arcDirection == ArcDirection::CLOCKWISE ? 1 : -1;
    int stepsError1 = 0;
    int stepsError2 = 0;

    for (int i = 0; i < arcMotion.totalSegment - 1; i++) {
        int lastAngle = normalizeAngle(arcMotion.startAngle + directionSign * i * ARC_ANGLE_PER_POINT);
        int nextAngle = normalizeAngle(arcMotion.startAngle + directionSign * (i + 1) * ARC_ANGLE_PER_POINT);
        
        float lastRad = lastAngle * PI / 180.0;
        float nextRad = nextAngle * PI / 180.0;

        int deltaSteps1 = (int)round(arcMotion.radiusSteps * (cos(nextRad) - cos(lastRad)));
        int deltaSteps2 = (int)round(arcMotion.radiusSteps * (sin(nextRad) - sin(lastRad)));

        int steps1 = deltaSteps1 + stepsError1;
        int steps2 = deltaSteps2 + stepsError2;

        if (abs(steps1) < MIN_MOTOR_STEPS) {
            stepsError1 = steps1;
            steps1 = 0;
        }
        else {
            stepsError1 = 0;
        }
        if (abs(steps2) < MIN_MOTOR_STEPS) {
            stepsError2 = steps2;
            steps2 = 0;
        }
        else {
            stepsError2 = 0;
        } // 100, 200

        if (i == arcMotion.totalSegment - 2) { // Handle the final movement to prevent the last steps is not enough
            int lastAngle = normalizeAngle(arcMotion.startAngle + directionSign * (i + 1) * ARC_ANGLE_PER_POINT);
            int nextAngle = normalizeAngle(arcMotion.startAngle + directionSign * (i + 2) * ARC_ANGLE_PER_POINT);
            
            float lastRad = lastAngle * PI / 180.0;
            float nextRad = nextAngle * PI / 180.0;

            int deltaSteps1 = (int)round(arcMotion.radiusSteps * (cos(nextRad) - cos(lastRad)));
            int deltaSteps2 = (int)round(arcMotion.radiusSteps * (sin(nextRad) - sin(lastRad)));

            int nextSteps1 = deltaSteps1;
            int nextSteps2 = deltaSteps2;
            
            if (abs(nextSteps1) < MIN_MOTOR_STEPS) {
                steps1 += (nextSteps1 + stepsError1);
                arcMotion.deltaX[i + 1] = 0;
            }
            else {
                arcMotion.deltaX[i + 1] = nextSteps1;
            }
            if (abs(nextSteps2) < MIN_MOTOR_STEPS) {
                steps2 += (nextSteps2 + stepsError2);
                arcMotion.deltaY[i + 1] = 0;
            }
            else {
                arcMotion.deltaY[i + 1] = nextSteps2;
            }
        }
        arcMotion.deltaX[i] = steps1;
        arcMotion.deltaY[i] = steps2;
    }
    for (int i = 0; i < arcMotion.totalSegment; i++) {
        Serial.print(arcMotion.deltaX[i]);
        Serial.print(", ");
        Serial.println(arcMotion.deltaY[i]);
    }
}

void updateArcMotion() { // Calculate and update the next ARC point
    if (!arcMotion.active) return;

    // Do not calculate the next point until the current segment is finished.
    if (motors[arcMotion.motor1Index].isMoving() || motors[arcMotion.motor2Index].isMoving()) return;

    if (arcMotion.currentSegment >= arcMotion.totalSegment) {
        arcMotion.active = false;
        return;
    }
    if (isArcMotionDebug) {
        Serial.print("ARC Motion: ");
        Serial.print(arcMotion.currentSegment);
        Serial.print("/");                                                            
        Serial.println(arcMotion.totalSegment);
    }

    int deltaSteps1 = arcMotion.deltaX[arcMotion.currentSegment];
    int deltaSteps2 = arcMotion.deltaY[arcMotion.currentSegment];

    Direction direction1 = deltaSteps1 >= 0 ? Direction::POSITIVE : Direction::NEGATIVE;
    Direction direction2 = deltaSteps2 >= 0 ? Direction::POSITIVE : Direction::NEGATIVE;

    int steps1 = abs(deltaSteps1);
    int steps2 = abs(deltaSteps2);

    // ARC ramp is controlled segment-by-segment here. Motor rampSteps stays 0.
    unsigned long segmentDelayUS = arcMotion.targetDelayUS;

    unsigned long delayUS1;
    unsigned long delayUS2;

    if (steps1 > steps2) { // steps1 need faster speed (smaller delayUS)
        delayUS1 = segmentDelayUS;
        if (arcMotion.currentSegment == 0 || arcMotion.currentSegment == arcMotion.totalSegment - 1) {
            delayUS2 = calculateSyncDelay(steps1, delayUS1, steps2);
        }
        else {
            delayUS2 = calculateSyncDelay(steps1, delayUS1, steps2, false);
        }
    }
    else {
        delayUS2 = segmentDelayUS;
        if (arcMotion.currentSegment == 0 || arcMotion.currentSegment == arcMotion.totalSegment - 1) {
            delayUS1 = calculateSyncDelay(steps2, delayUS2, steps1);
        }
        else {
            delayUS1 = calculateSyncDelay(steps2, delayUS2, steps1, false);
        }
    }
    
    delayUS1 = checkDelayUSLimit(delayUS1);
    delayUS2 = checkDelayUSLimit(delayUS2);
    
    if (isArcMotionDebug) {
        Serial.print("Arc Motor1: direction=");
        Serial.print(direction1 == Direction::POSITIVE ? "+" : "-");
        Serial.print(", steps=");
        Serial.print(steps1);
        Serial.print(", delay=");
        Serial.print(delayUS1);
        Serial.print("; Arc Motor2: direction=");
        Serial.print(direction2 == Direction::POSITIVE ? "+" : "-");
        Serial.print(", steps=");
        Serial.print(steps2);
        Serial.print(", delay=");
        Serial.println(delayUS2);
    }
    
    if (arcMotion.currentSegment == 0) {
        motors[arcMotion.motor1Index].setMotion(direction1, steps1, MAX_DELAY_US, delayUS1, true);
        motors[arcMotion.motor2Index].setMotion(direction2, steps2, MAX_DELAY_US, delayUS2, true);
    }
    else if (arcMotion.currentSegment == arcMotion.totalSegment - 1) {
        motors[arcMotion.motor1Index].setMotion(direction1, steps1, delayUS1, MAX_DELAY_US, false);
        motors[arcMotion.motor2Index].setMotion(direction2, steps2, delayUS2, MAX_DELAY_US, false);
    }
    else {
        int lastDelayUS1 = motors[arcMotion.motor1Index].getCurrentDelayUS();
        int lastDelayUS2 = motors[arcMotion.motor2Index].getCurrentDelayUS();
        motors[arcMotion.motor1Index].setMotion(direction1, steps1, lastDelayUS1, delayUS1, true);
        motors[arcMotion.motor2Index].setMotion(direction2, steps2, lastDelayUS2, delayUS2, true);
    }

    arcMotion.currentSegment++;
}

// ==================================================
// Motor Update
// ==================================================
void updateMotors() {
    for (int i = 0; i < MOTOR_COUNT; i++) {
        motors[i].updateMotion();
        if (motors[i].isUpdated() && isMotorDebug) {
            if (motors[i].getCurrentStep() == motors[i].getTotalSteps()-1){
                Serial.print("Time Stamp: ");
                Serial.print(millis());
                Serial.print(", M");
                Serial.print(i);
                Serial.print(": Step ");
                Serial.print(motors[i].getCurrentStep());
                Serial.print("/");
                Serial.print(motors[i].getTotalSteps());
                Serial.print(", Delay ");
                Serial.println(motors[i].getCurrentDelayUS());
            }
        }
    }
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
    updateLinearMotion();
    updateArcMotion();
    updateMotors();
}


// //
// Since you want to control four motors with a Motor class and a non-blocking MotionController, I would retain your current trigonometric approach for calculating the circular coordinates, but improve the execution architecture:
// - ArcPlanner: Calculate the next X/Y targets using cos() and sin().
// - MotionController: Calculate the movement duration and coordinate both motors.
// - Motor: Generate direction signals and STEP pulses at the scheduled times.
// The key distinction is that your present updateArcMotion() combines path geometry and motor timing calculations. Separating them will make both linear and circular motion easier to maintain and test.
// The most important concept: Your program does not directly command a motor to rotate in a circle. It calculates successive X/Y displacements along the circle and coordinates two linear motors to follow them.
// //