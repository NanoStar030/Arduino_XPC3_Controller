// main_muti_motor.cpp
// Function:
// 1. Receive commands from serial port
// 2. Control multiple stepper motors with acceleration and deceleration

// Maintanance:
// 1. Only activate one motor at a time.
// 2. The distance and the speed will be determined by the number of steps and the time interval.

#include "main.h" // Motor parameters here
#include <math.h>
#include <Arduino.h>

Motor motors[] = {
    {11, 10, 99},    // Motor 0 {step, dir}
    {13, 12, 99},    // Motor 1 {step, dir}
    {6, 7, 9},      // Motor 2 {step, dir}
    {4, 5, 8}       // Motor 3 {step, dir}
};

char lineBuf[LINE_BUF_SIZE];
int lineIndex = 0;
SystemState systemState = SystemState::IDLE;

SingleMoveCommand current_s_Move;
SingleMotionRuntime s_Motion;

DualMoveCommand current_d_Move;
DualMotionRuntime d_Motion;
//==================================================

void S_stepPulse(Motor &motor, unsigned long highPulseUS = 10){
    digitalWrite(motor.stepPin, HIGH);
    delayMicroseconds(highPulseUS); // Drv8825 requires a minimum pulse width of 1.9us, so we use 3us to be safe
    digitalWrite(motor.stepPin, LOW);
}

void D_stepPulse(bool stepA, bool stepB, unsigned long highPulseUS = 10)
{
    if (stepA) {
        digitalWrite(motors[current_d_Move.motorA].stepPin, HIGH);
    }

    if (stepB) {
        digitalWrite(motors[current_d_Move.motorB].stepPin, HIGH);
    }

    delayMicroseconds(highPulseUS);

    if (stepA) {
        digitalWrite(motors[current_d_Move.motorA].stepPin, LOW);
    }

    if (stepB) {
        digitalWrite(motors[current_d_Move.motorB].stepPin, LOW);
    }
}

const char* getSystemStateName(SystemState state)
{
    switch (state)
    {
        case SystemState::IDLE:     return "IDLE";
        case SystemState::S_RUNNING:  return "SINGLE_RUNNING";
        case SystemState::D_RUNNING:    return "DUAL_RUNNING";
        case SystemState::ERROR:    return "ERROR";
        default:                    return "UNKNOWN";
    }
}

//==================================================

void start_S_Motion() {
    s_Motion.stepNUM = current_s_Move.stepNUM;
    s_Motion.rampStepNUM  = current_s_Move.rampStepNUM;

    s_Motion.stepIdx = 0;
    s_Motion.startDelay = START_DELAY_US;
    s_Motion.minDelay   = MIN_DELAY_US;

    if (s_Motion.rampStepNUM > 0) {
        s_Motion.delta = (s_Motion.startDelay - s_Motion.minDelay) / s_Motion.rampStepNUM;
    }
    else {
        s_Motion.delta  = 1;
    }

    if (s_Motion.delta < 1)
        s_Motion.delta = 1;

    s_Motion.currentDelay = s_Motion.startDelay;
    s_Motion.lastStepUS = micros();

    digitalWrite(motors[current_s_Move.motorIdx].dirPin, current_s_Move.dir == Direction::POSITIVE);
    if (motors[current_s_Move.motorIdx].enaPin != 99) { // enable the motor if the enable pin existed
        digitalWrite(motors[current_s_Move.motorIdx].enaPin, LOW);
    }
    systemState = SystemState::S_RUNNING;
}
void handle_S_MoveCommand(char *cmd) {
    char *token;
    token = strtok(cmd, " ");   // MOVE
    // Motor
    token = strtok(NULL, " ");
    if (token == NULL) {
        Serial.println("[ERROR] Missing Parameter...");
        return;
    }
    current_s_Move.motorIdx = atoi(token);
    if (current_s_Move.motorIdx < 0 || current_s_Move.motorIdx >= MOTOR_COUNT) {
        Serial.println("ERROR Invalid Motor...");
        return;
    }

    // Direction
    token = strtok(NULL, " ");
    if (token == NULL) {
        Serial.println("[ERROR] Missing Parameter...");
        return;
    }
    int dir = atoi(token);
    if (dir == 0) {
        current_s_Move.dir = Direction::NEGATIVE;
    }
    else if (dir == 1) {
        current_s_Move.dir = Direction::POSITIVE;
    }
    else {
        Serial.println("[ERROR] Direction Must Be 0 or 1...");
        return;
    }

    // Steps
    token = strtok(NULL, " ");
    if (token == NULL) {
        Serial.println("[ERROR] Missing Parameter...");
        return;
    }
    current_s_Move.stepNUM = atoi(token);

    // Step Delay
    token = strtok(NULL, " ");
    if (token == NULL) {
        Serial.println("[ERROR] Missing Parameter...");
        return;
    }
    current_s_Move.stepDelayUS = atoi(token);

    // Ramp
    token = strtok(NULL, " ");
    if (token == NULL) {
        Serial.println("[ERROR] Missing Parameter...");
        return;
    }
    current_s_Move.rampStepNUM = atoi(token);

    // Finish handling the move command 
    start_S_Motion();
    Serial.print("[OK] Motor ");
    Serial.print(current_s_Move.motorIdx);
    Serial.println(" is Running...");
    Serial.print("Speed: ");
    Serial.println(current_s_Move.stepDelayUS);
}
void start_D_Motion() {
    d_Motion.totalStepsA = labs(current_d_Move.stepsA);
    d_Motion.totalStepsB = labs(current_d_Move.stepsB);

    d_Motion.totalTicks = max(d_Motion.totalStepsA, d_Motion.totalStepsB);

    d_Motion.tickIndex = 0;
    d_Motion.accumulatorA = 0;
    d_Motion.accumulatorB = 0;
    d_Motion.rampTicks = current_d_Move.rampStepNUM;

    // Ramp cannot be negative
    if (d_Motion.rampTicks < 0) {
        d_Motion.rampTicks = 0;
    }

    // Acceleration + deceleration cannot exceed total motion
    if (2L * d_Motion.rampTicks > d_Motion.totalTicks) {
        d_Motion.rampTicks = d_Motion.totalTicks / 2;
    }

    d_Motion.startDelayUS = START_DELAY_US;

    // Target speed comes from the D_MOVE command
    d_Motion.minDelayUS = current_d_Move.stepDelayUS;

    // Start speed should not be faster than target speed
    if (d_Motion.startDelayUS < d_Motion.minDelayUS) {
        d_Motion.startDelayUS = d_Motion.minDelayUS;
    }

    if (d_Motion.rampTicks > 0) {
        d_Motion.deltaDelayUS = (d_Motion.startDelayUS - d_Motion.minDelayUS) / d_Motion.rampTicks;
        if (d_Motion.deltaDelayUS < 1) {
            d_Motion.deltaDelayUS = 1;
        }
    }
    else {
        d_Motion.deltaDelayUS = 0;
    }

    // Start from slow speed
    d_Motion.currentDelayUS = d_Motion.startDelayUS;

    // Record current time
    d_Motion.lastStepUS = micros();
    digitalWrite(motors[current_d_Move.motorA].dirPin, current_d_Move.stepsA >= 0 ? HIGH : LOW);
    digitalWrite(motors[current_d_Move.motorB].dirPin, current_d_Move.stepsB >= 0 ? HIGH : LOW);
    if (d_Motion.totalStepsA > 0 && motors[current_d_Move.motorA].enaPin != 99) {
        digitalWrite(motors[current_d_Move.motorA].enaPin, LOW);
    }
    if (d_Motion.totalStepsB > 0 && motors[current_d_Move.motorB].enaPin != 99) {
        digitalWrite(motors[current_d_Move.motorB].enaPin, LOW);
    }
    systemState = SystemState::D_RUNNING;
}

void handle_D_MoveCommand(char *cmd)
{
    char *token;
    // ==========================================
    // Command: D_MOVE
    // ==========================================
    token = strtok(cmd, " ");
    // ==========================================
    // Motor A
    // ==========================================
    token = strtok(NULL, " ");
    if (token == NULL) {
        Serial.println("[ERROR] Missing Motor A...");
        return;
    }
    current_d_Move.motorA = atoi(token);
    if ( current_d_Move.motorA < 0 || current_d_Move.motorA >= MOTOR_COUNT) {
        Serial.println("[ERROR] Invalid Motor A...");
        return;
    }

    // ==========================================
    // Steps A
    // ==========================================
    token = strtok(NULL, " ");
    if (token == NULL) {
        Serial.println("[ERROR] Missing Steps A...");
        return;
    }
    current_d_Move.stepsA = atol(token);

    // ==========================================
    // Motor B
    // ==========================================
    token = strtok(NULL, " ");
    if (token == NULL) {
        Serial.println("[ERROR] Missing Motor B...");
        return;
    }

    current_d_Move.motorB = atoi(token);
    if ( current_d_Move.motorB < 0 || current_d_Move.motorB >= MOTOR_COUNT ) {
        Serial.println("[ERROR] Invalid Motor B...");
        return;
    }

    // ==========================================
    // Steps B
    // ==========================================
    token = strtok(NULL, " ");
    if (token == NULL) {
        Serial.println("[ERROR] Missing Steps B...");
        return;
    }
    current_d_Move.stepsB = atol(token);

    // ==========================================
    // Step Delay
    // ==========================================
    token = strtok(NULL, " ");
    if (token == NULL) {
        Serial.println("[ERROR] Missing Step Delay...");
        return;
    }

    current_d_Move.stepDelayUS = atol(token);
    if (current_d_Move.stepDelayUS == 0) {
        Serial.println("[ERROR] Step Delay Must Be > 0...");
        return;
    }

    // ==========================================
    // Ramp
    // ==========================================
    token = strtok(NULL, " ");

    if (token == NULL) {
        Serial.println("[ERROR] Missing Ramp...");
        return;
    }
    current_d_Move.rampStepNUM = atol(token);
    if (current_d_Move.rampStepNUM < 0) {
        Serial.println("[ERROR] Ramp Must Be >= 0...");
        return;
    }

    // ==========================================
    // Motor A and B cannot be the same
    // ==========================================
    if (current_d_Move.motorA == current_d_Move.motorB) {
        Serial.println("[ERROR] Motor A and Motor B Cannot Be The Same...");
        return;
    }

    // ==========================================
    // Both motors cannot have zero steps
    // ==========================================
    if ( current_d_Move.stepsA == 0 && current_d_Move.stepsB == 0 ) {
        Serial.println("[ERROR] Both Steps Cannot Be Zero...");
        return;
    }

    // ==========================================
    // Start Dual Motion
    // ==========================================
    start_D_Motion();
    Serial.println("[OK] Dual Motion Started...");
    Serial.print("Motor A: ");
    Serial.print(current_d_Move.motorA);
    Serial.print("Motor B: ");
    Serial.print(current_d_Move.motorB);
}

//==================================================
void handleCommand(char *cmd) {
    // Handle the *lineBuf* received from updateSerial() and execute the corresponding command.
    if (strlen(cmd) == 0)
        return;

    if (strcmp(cmd, "PING") == 0) {
        Serial.println("PONG");
    }

    else if (strcmp(cmd, "STATUS") == 0) {
        Serial.println(getSystemStateName(systemState));
    }

    else if (strcmp(cmd, "STOP") == 0) {
        if (systemState == SystemState::S_RUNNING) {
            if (motors[current_s_Move.motorIdx].enaPin != 99) { // make a stop function
                digitalWrite(motors[current_s_Move.motorIdx].enaPin, HIGH);
            }
            systemState = SystemState::IDLE;
            Serial.println("[OK] Stopping the motor...");
        } 
        else if (systemState == SystemState::D_RUNNING) {
            // Disable Motor A
            if (motors[current_d_Move.motorA].enaPin != 99) {
                digitalWrite(motors[current_d_Move.motorA].enaPin, HIGH);
            }
            // Disable Motor B
            if (motors[current_d_Move.motorB].enaPin != 99) {
                digitalWrite(motors[current_d_Move.motorB].enaPin, HIGH);
            }
            systemState = SystemState::IDLE;
            Serial.println("[OK] Stopping the motor...");
        }
        else {
            Serial.println("[Error] Not Running Now...");
        }
    }

    else if (strcmp(cmd, "GET_CONFIG") == 0){
        Serial.println("[OK] Returning the Configs...");
        Serial.print("CONFIG motors_number=");
        Serial.print(MOTOR_COUNT);

        Serial.print(" start_delau_us=");
        Serial.print(START_DELAY_US);

        Serial.print(" max_delau_us=");
        Serial.println(MAX_DELAY_US);
    }

    else if (strncmp(cmd, "S_MOVE", 4) == 0) {
        if (systemState != SystemState::IDLE) {
            Serial.println("[Error] System is Busy Now...");
            return;
        }
        handle_S_MoveCommand(cmd);
    }
    else if (strncmp(cmd, "D_MOVE", 4) == 0) {
        if (systemState != SystemState::IDLE) {
            Serial.println("[Error] System is Busy Now...");
            return;
        }
        handle_D_MoveCommand(cmd);
    }
    else {
        Serial.println("[Error] Unknown Command...");
    }
}

void updateSerial() { 
    // Read serial input and package it into *lineBuf*
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            lineBuf[lineIndex] = '\0';
            if (lineIndex > 0) {
                handleCommand(lineBuf);
            }
            lineIndex = 0;
        }
        else {
            if (lineIndex < LINE_BUF_SIZE - 1) {
                lineBuf[lineIndex++] = c;
            }
        }
    }
}

void update_S_Motion() {
    if (systemState != SystemState::S_RUNNING)
        return;

    unsigned long now = micros();
    if (now - s_Motion.lastStepUS < s_Motion.currentDelay) // unit not the same
        return;
    s_Motion.lastStepUS = now;
    S_stepPulse(motors[current_s_Move.motorIdx]);
    s_Motion.stepIdx++;

    //========================
    // Acceleration
    //========================
    if (s_Motion.stepIdx < s_Motion.rampStepNUM) {
        if (s_Motion.currentDelay > s_Motion.minDelay + s_Motion.delta)
            s_Motion.currentDelay -= s_Motion.delta;
        else
            s_Motion.currentDelay = s_Motion.minDelay;
    }

    //========================
    // Deceleration
    //========================
    else if (s_Motion.stepIdx >= s_Motion.stepNUM- s_Motion.rampStepNUM) {
        s_Motion.currentDelay += s_Motion.delta;

        if (s_Motion.currentDelay > s_Motion.startDelay)
            s_Motion.currentDelay = s_Motion.startDelay;
    }

    //========================
    // Constant Speed
    //========================
    else {
        s_Motion.currentDelay = s_Motion.minDelay;
    }

    //========================
    // Finished
    //========================
    if (s_Motion.stepIdx >= s_Motion.stepNUM) {
        systemState = SystemState::IDLE;
        if (motors[current_s_Move.motorIdx].enaPin != 99) { // make a stop function
            digitalWrite(motors[current_s_Move.motorIdx].enaPin, HIGH);
        }
        Serial.println("DONE");
    }
}

void update_D_Motion() {
    // ==================================================
    // 2. Only run during DUAL_RUNNING
    // ==================================================
    if (systemState != SystemState::D_RUNNING) {
        return;
    }

    // ==================================================
    // 3. Check step timing
    // ==================================================
    unsigned long now = micros();
    if (now - d_Motion.lastStepUS < d_Motion.currentDelayUS) {
        return;
    }
    d_Motion.lastStepUS = now;

    // ==================================================
    // 4. DDA / Bresenham accumulator
    // ==================================================
    d_Motion.accumulatorA += d_Motion.totalStepsA;
    d_Motion.accumulatorB += d_Motion.totalStepsB;

    bool stepA = false;
    bool stepB = false;

    // ---------------- Motor A ----------------
    if (d_Motion.accumulatorA >= d_Motion.totalTicks) {
        d_Motion.accumulatorA -= d_Motion.totalTicks;
        stepA = true;
    }
    // ---------------- Motor B ----------------
    if (d_Motion.accumulatorB >= d_Motion.totalTicks) {
        d_Motion.accumulatorB -= d_Motion.totalTicks;
        stepB = true;
    }
    // ==================================================
    // 5. Generate pulse
    // ==================================================
    D_stepPulse(stepA, stepB);
    d_Motion.tickIndex++;

    // ==================================================
    // 6. Acceleration
    // ==================================================
    if (d_Motion.rampTicks > 0 && d_Motion.tickIndex < d_Motion.rampTicks){
        if (d_Motion.currentDelayUS > d_Motion.minDelayUS + d_Motion.deltaDelayUS) {
            d_Motion.currentDelayUS -= d_Motion.deltaDelayUS;
        }
        else {
            d_Motion.currentDelayUS = d_Motion.minDelayUS;
        }
    }

    // ==================================================
    // 7. Deceleration
    // ==================================================
    else if (d_Motion.rampTicks > 0 && d_Motion.tickIndex >= d_Motion.totalTicks - d_Motion.rampTicks) {
        d_Motion.currentDelayUS += d_Motion.deltaDelayUS;

        if (d_Motion.currentDelayUS > d_Motion.startDelayUS) {
            d_Motion.currentDelayUS =
                d_Motion.startDelayUS;
        }
    }

    // ==================================================
    // 8. Constant Speed
    // ==================================================
    else{
        d_Motion.currentDelayUS = d_Motion.minDelayUS;
    }

    // ==================================================
    // 9. Motion Finished
    // ==================================================
    if (d_Motion.tickIndex >= d_Motion.totalTicks) {
        // Disable Motor A
        if (motors[current_d_Move.motorA].enaPin != 99) {
            digitalWrite(
                motors[current_d_Move.motorA].enaPin,
                HIGH
            );
        }

        // Disable Motor B
        if (motors[current_d_Move.motorB].enaPin != 99) {
            digitalWrite(
                motors[current_d_Move.motorB].enaPin,
                HIGH
            );
        }
        systemState = SystemState::IDLE;
        Serial.println("DONE");
    }
}

void setup() {
    // 初始化所有馬達
    for (int i = 0; i < MOTOR_COUNT; i++) {
        pinMode(motors[i].stepPin, OUTPUT);
        pinMode(motors[i].dirPin, OUTPUT);
        digitalWrite(motors[i].stepPin, LOW);
        digitalWrite(motors[i].dirPin, LOW);
        if (motors[i].enaPin != 99) {
            pinMode(motors[i].enaPin, OUTPUT);
            digitalWrite(motors[i].enaPin, HIGH);
        }
    }
    Serial.begin(115200);
    delay(500);
    Serial.println("READY");
}

void loop() {
    updateSerial();
    update_S_Motion();
    update_D_Motion();
}