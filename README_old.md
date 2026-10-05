# Multi-Stepper Motor Controller 程式結構與運行流程

## 1. 程式概述

此程式是一個基於 Arduino 的多步進馬達控制系統。

主要功能包含：

- 控制 4 顆步進馬達
- 透過 Serial 接收控制命令
- 一次只允許一顆馬達運行
- 支援加速、定速、減速三階段運動
- 支援運行期間停止馬達
- 使用狀態機管理系統狀態
- 使用 `micros()` 進行非阻塞式 Step Pulse 排程

整體架構可分為：

1. 資料結構與參數定義
2. Serial 命令接收
3. 命令解析
4. 運動參數設定
5. 馬達運動更新
6. 系統狀態管理

---

## 2. 檔案結構

```text
Project
│
├── main.h
│   ├── Motor 定義
│   ├── Direction 定義
│   ├── MoveCommand 定義
│   ├── MotionRuntime 定義
│   ├── SystemState 定義
│   ├── 馬達參數
│   ├── Serial Buffer 設定
│   └── Global State 宣告
│
└── main_muti_motor.cpp
    ├── 馬達 Pin 設定
    ├── Global Variables
    ├── OneStepPulse()
    ├── getSystemStateName()
    ├── handleMoveCommand()
    ├── handleCommand()
    ├── updateSerial()
    ├── updateMotion()
    ├── setup()
    └── loop()
```

---

## 3. `main.h` 結構

### 3.1 Motor

```cpp
struct Motor {
    int stepPin;
    int dirPin;
};
```

每一顆步進馬達由兩個 Pin 控制：

- `stepPin`：每產生一次 Pulse，馬達移動一步。
- `dirPin`：控制馬達旋轉方向。

目前系統定義：

```cpp
constexpr int MOTOR_COUNT = 4;
```

因此總共有 4 顆馬達。

---

### 3.2 Direction

```cpp
enum class Direction {
    NEGATIVE,
    POSITIVE,
};
```

用來表示馬達運動方向。

| Direction | Serial 數值 | 意義 |
|---|---:|---|
| `NEGATIVE` | `0` | 負方向 |
| `POSITIVE` | `1` | 正方向 |

---

### 3.3 MoveCommand

```cpp
struct MoveCommand {
    int motorIdx;
    Direction dir;
    int stepNUM;
    int stepDelayUS;
    int rampStepNUM;
};
```

`MoveCommand` 用來儲存使用者輸入的 MOVE 命令。

例如：

```text
MOVE 0 1 1000 200 100
```

代表：

| 參數 | 值 |
|---|---:|
| Motor | 0 |
| Direction | Positive |
| Steps | 1000 |
| Step Delay | 200 us |
| Ramp Steps | 100 |

各欄位用途：

| 參數 | 功能 |
|---|---|
| `motorIdx` | 選擇哪一顆馬達 |
| `dir` | 馬達方向 |
| `stepNUM` | 總移動 Step 數 |
| `stepDelayUS` | 目標 Step 間隔 |
| `rampStepNUM` | 加速與減速區間的 Step 數 |

---

### 3.4 MotionRuntime

```cpp
struct MotionRuntime {
    int stepNUM;
    int rampStepNUM;
    int stepIdx;
    unsigned long startDelay;
    unsigned long minDelay;
    unsigned long currentDelay;
    unsigned long delta;
    unsigned long lastStepUS;
};
```

`MotionRuntime` 儲存馬達運動過程中會持續改變的執行資料。

概念上：

```text
MoveCommand
    ↓
使用者要求「要怎麼動」

MotionRuntime
    ↓
系統記錄「目前動到哪裡」
```

主要變數：

| 變數 | 說明 |
|---|---|
| `stepNUM` | 總步數 |
| `rampStepNUM` | 加速 / 減速步數 |
| `stepIdx` | 目前已完成的 Step |
| `startDelay` | 起始 Step 間隔 |
| `minDelay` | 最高速度時的 Step 間隔 |
| `currentDelay` | 目前使用的 Step 間隔 |
| `delta` | 每一步改變多少 Delay |
| `lastStepUS` | 上一次 Step 的時間 |

速度和 Delay 的關係：

```text
Delay 越大
    ↓
Step Pulse 越慢
    ↓
馬達速度越慢
```

反之：

```text
Delay 越小
    ↓
Step Pulse 越快
    ↓
馬達速度越快
```

---

### 3.5 SystemState

```cpp
enum class SystemState {
    IDLE,
    RUNNING,
    STOPPING,
    ERROR,
};
```

系統使用狀態機管理目前狀態。

```text
              MOVE
      ┌──────────────────┐
      │                  ▼
    IDLE              RUNNING
      ▲                  │
      │                  │ STOP
      │                  ▼
      └────────────── STOPPING
```

狀態說明：

| State | 意義 |
|---|---|
| `IDLE` | 系統待機，可以接受 MOVE |
| `RUNNING` | 馬達正在運行 |
| `STOPPING` | 收到 STOP，準備停止 |
| `ERROR` | 錯誤狀態，目前尚未完整使用 |

---

## 4. Motor Pin 設定

程式中定義四顆馬達：

```cpp
Motor motors[] = {
    {11, 10},    // Motor 0
    {13, 12},    // Motor 1
    {6, 7},      // Motor 2
    {8, 9}       // Motor 3
};
```

格式為：

```text
{STEP Pin, DIR Pin}
```

| Motor | STEP | DIR |
|---:|---:|---:|
| 0 | 11 | 10 |
| 1 | 13 | 12 |
| 2 | 6 | 7 |
| 3 | 8 | 9 |

---

## 5. 程式啟動流程

Arduino 啟動後首先執行：

```cpp
setup()
```

主要工作：

```text
setup()
 │
 ├── 初始化所有 STEP Pin
 ├── 初始化所有 DIR Pin
 ├── STEP = LOW
 ├── DIR = LOW
 ├── Serial.begin(115200)
 └── 印出 "READY"
```

完成初始化後，程式進入：

```cpp
loop()
```

---

## 6. 主迴圈

主程式：

```cpp
void loop() {
    updateSerial();
    updateMotion();
}
```

流程：

```text
┌──────────────────────┐
│       Arduino        │
│        loop()        │
└──────────┬───────────┘
           │
           ▼
    updateSerial()
           │
           ▼
    updateMotion()
           │
           └──────────────┐
                          │
                          ▼
                    下一次 loop()
```

這種設計的主要特色是：

> 不使用長時間 Blocking Loop 控制馬達。

因此即使馬達正在運行，Serial 仍然可以持續被讀取，例如可以在運動時送出：

```text
STOP
```

讓系統停止運動。

---

## 7. Serial 接收流程

Serial 接收由：

```cpp
updateSerial()
```

負責。

流程：

```text
Serial.available()
       │
       ▼
 Serial.read()
       │
       ▼
 是 '\n' / '\r' ?
       │
   ┌───┴────┐
  Yes       No
   │         │
   ▼         ▼
結束字串   存入 lineBuf
   │
   ▼
handleCommand()
```

程式使用：

```cpp
char lineBuf[LINE_BUF_SIZE];
```

暫存輸入命令。

當收到 `\n` 或 `\r` 時，代表一條命令結束，接著執行：

```cpp
handleCommand(lineBuf);
```

---

## 8. Command Parser

所有命令都由：

```cpp
handleCommand()
```

進行判斷。

目前支援：

```text
PING
STATUS
STOP
GET_CONFIG
MOVE
```

### 8.1 PING

輸入：

```text
PING
```

回傳：

```text
PONG
```

可用來確認 PC 與 Arduino 的 Serial 通訊是否正常。

### 8.2 STATUS

輸入：

```text
STATUS
```

回傳目前系統狀態，例如：

```text
IDLE
```

或：

```text
RUNNING
```

### 8.3 GET_CONFIG

輸入：

```text
GET_CONFIG
```

系統回傳基本設定，例如：

```text
CONFIG motors_number=4 start_speed_mm_s=0.5 max_speed_mm_s=30
```

### 8.4 STOP

輸入：

```text
STOP
```

如果目前：

```cpp
systemState == SystemState::RUNNING
```

則系統切換為：

```cpp
systemState = SystemState::STOPPING;
```

下一次執行 `updateMotion()` 時：

```text
STOPPING
   │
   ▼
 IDLE
```

並輸出：

```text
STOPPED
```

---

## 9. MOVE 命令

MOVE 是主要的運動控制命令。

格式：

```text
MOVE <motor> <direction> <steps> <stepDelayUS> <rampSteps>
```

例如：

```text
MOVE 2 1 1000 200 100
```

代表：

| 參數 | 值 |
|---|---:|
| Motor | 2 |
| Direction | Positive |
| Steps | 1000 |
| Step Delay | 200 us |
| Ramp Steps | 100 |

---

## 10. MOVE 命令處理流程

首先 `handleCommand()` 確認：

```cpp
systemState == SystemState::IDLE
```

只有 `IDLE` 狀態才允許開始新的 MOVE。

否則回傳：

```text
[Error] System is Busy Now...
```

接著由：

```cpp
handleMoveCommand()
```

解析參數。

流程：

```text
MOVE command
     │
     ▼
Parse Motor
     │
     ▼
Check Motor Range
     │
     ▼
Parse Direction
     │
     ▼
Check Direction 0 / 1
     │
     ▼
Parse Steps
     │
     ▼
Parse Step Delay
     │
     ▼
Parse Ramp Steps
     │
     ▼
systemState = RUNNING
```

---

## 11. 馬達 Step Pulse

實際產生馬達 Step 的函式：

```cpp
OneStepPulse()
```

執行：

```text
STEP = HIGH
    │
    ▼
delayMicroseconds(3)
    │
    ▼
STEP = LOW
```

波形概念：

```text
       ┌───┐
       │   │ 3 us
───────┘   └────────
        1 STEP
```

程式註解指出 DRV8825 最低 Pulse Width 約為 `1.9 us`，因此使用 `3 us` 提供安全裕量。

---

## 12. `updateMotion()`

`updateMotion()` 是整個運動控制的核心。

流程：

```text
updateMotion()
     │
     ├── STOPPING ?
     │      │
     │      └── Yes → IDLE → STOPPED
     │
     ├── RUNNING ?
     │      │
     │      └── No → return
     │
     ▼
取得 micros()
     │
     ▼
是否到下一 Step 時間？
     │
  ┌──┴───┐
 No      Yes
 │        │
return    ▼
      OneStepPulse()
          │
          ▼
       stepIdx++
          │
          ▼
     更新速度 Ramp
          │
          ▼
     是否完成？
```

---

## 13. 非阻塞式 Step Timing

程式透過：

```cpp
unsigned long now = micros();
```

取得目前時間。

接著判斷：

```cpp
if (now - motion.lastStepUS < motion.currentDelay)
    return;
```

概念：

```text
現在時間 - 上次 Step 時間
             │
             ▼
       >= currentDelay ?
          │
      ┌───┴───┐
     No      Yes
      │        │
   return   產生 Step
```

因此不需要長時間使用：

```cpp
delay(...)
```

等待下一個 Step。

這使得主迴圈可以同時：

```text
讀取 Serial
+
控制馬達
```

---

## 14. 加速 / 定速 / 減速

整個運動可分成三段：

```text
Speed
  ▲
  │          ┌─────────────┐
  │         /               \
  │        /                 \
  │       /                   \
  │______/                     \______
  │
  └───────────────────────────────► Step
       Accel       Constant      Decel
```

分別為：

1. Acceleration
2. Constant Speed
3. Deceleration

---

### 14.1 Acceleration

條件：

```cpp
motion.stepIdx < motion.rampStepNUM
```

程式：

```cpp
motion.currentDelay -= motion.delta;
```

Delay 逐漸減少：

```text
500 us
 ↓
450 us
 ↓
400 us
 ↓
350 us
 ↓
300 us
```

因此 Step Frequency 增加，馬達逐漸加速。

---

### 14.2 Constant Speed

如果目前 Step 不在加速或減速區：

```cpp
motion.currentDelay = motion.minDelay;
```

因此 Step Delay 固定，馬達保持目標速度。

---

### 14.3 Deceleration

條件：

```cpp
motion.stepIdx >= motion.stepNUM - motion.rampStepNUM
```

執行：

```cpp
motion.currentDelay += motion.delta;
```

Delay 逐漸增加：

```text
300 us
 ↓
350 us
 ↓
400 us
 ↓
450 us
 ↓
500 us
```

Step Frequency 降低，馬達逐漸減速。

---

## 15. 運動完成

每執行一次 Step：

```cpp
motion.stepIdx++;
```

當：

```cpp
motion.stepIdx >= motion.stepNUM
```

代表運動完成。

系統設定：

```cpp
systemState = SystemState::IDLE;
```

並輸出：

```text
DONE
```

狀態變化：

```text
RUNNING
   │
   │ stepIdx >= stepNUM
   ▼
 IDLE
```

---

## 16. 完整資料流

```text
PC / Python / Serial Monitor
            │
            │ Serial
            ▼
      updateSerial()
            │
            ▼
        lineBuf
            │
            ▼
     handleCommand()
            │
      ┌─────┼─────────────┐
      │     │             │
     PING STATUS         MOVE
                          │
                          ▼
                handleMoveCommand()
                          │
                          ▼
                    currentMove
                          │
                          ▼
                systemState=RUNNING
                          │
                          ▼
                    updateMotion()
                          │
              ┌───────────┼───────────┐
              │           │           │
             Accel     Constant      Decel
              │           │           │
              └───────────┼───────────┘
                          │
                          ▼
                    OneStepPulse()
                          │
                          ▼
                       Motor
```

---

## 17. System State Flow

```text
                  Arduino Boot
                       │
                       ▼
                     IDLE
                       │
                       │ MOVE
                       ▼
                    RUNNING
                    /     \
                   /       \
              STOP          DONE
                │             │
                ▼             │
            STOPPING          │
                │             │
                └──────┬──────┘
                       ▼
                     IDLE
```

---

## 18. 程式架構特色

### 18.1 Serial 與 Motion 分離

程式沒有把馬達控制全部放在 `handleMoveCommand()`，而是分成：

```text
handleMoveCommand()
      │
      └── 解析與建立 Motion Command

updateMotion()
      │
      └── 真正執行 Motion
```

讓命令解析與硬體運動控制分離。

### 18.2 使用 State Machine

透過 `SystemState` 統一管理：

```text
IDLE
RUNNING
STOPPING
ERROR
```

可以避免同時執行多個 MOVE。

### 18.3 非阻塞式運動控制

使用 `micros()` 而不是長時間 `delay()`，因此主迴圈仍然能處理 Serial。

這對以下即時命令很重要：

```text
STOP
STATUS
```

### 18.4 Command 與 Runtime 分開

程式將 `MoveCommand` 和 `MotionRuntime` 分開：

```text
MoveCommand
    =
使用者希望怎麼動

MotionRuntime
    =
目前正在怎麼動
```

這是一個適合後續擴充的架構。

---

## 19. 目前程式需要注意的地方

目前 `handleMoveCommand()` 解析完成後，主要將輸入值存進 `currentMove`，然後執行：

```cpp
systemState = SystemState::RUNNING;
```

但是 `updateMotion()` 實際使用的是 `motion`。

因此在正式開始 MOVE 前，還需要確認是否有完整初始化下列 runtime 參數：

```cpp
motion.stepNUM;
motion.rampStepNUM;
motion.stepIdx;
motion.startDelay;
motion.minDelay;
motion.currentDelay;
motion.delta;
motion.lastStepUS;
```

目前提供的程式中，沒有看到這些值在 `handleMoveCommand()` 後被完整設定。

另外，MOVE 命令雖然已經將方向解析到：

```cpp
currentMove.dir
```

但目前提供的程式中，也沒有看到在開始運動時將方向實際寫入：

```cpp
motors[currentMove.motorIdx].dirPin
```

因此 Direction Pin 的設定也需要補齊。

---

## 20. 建議的完整運行流程

較完整的架構可以是：

```text
Serial MOVE Command
        │
        ▼
handleCommand()
        │
        ▼
handleMoveCommand()
        │
        ├── Validate Motor
        ├── Validate Direction
        ├── Validate Steps
        ├── Validate Delay
        └── Validate Ramp
        │
        ▼
initializeMotion()
        │
        ├── 設定 DIR Pin
        ├── motion.stepIdx = 0
        ├── 設定 motion.stepNUM
        ├── 設定 motion.rampStepNUM
        ├── 設定 startDelay
        ├── 設定 minDelay
        ├── 計算 delta
        └── 記錄 lastStepUS
        │
        ▼
systemState = RUNNING
        │
        ▼
updateMotion()
        │
        ├── Acceleration
        ├── Constant Speed
        ├── Deceleration
        └── OneStepPulse()
        │
        ▼
stepIdx >= stepNUM
        │
        ▼
systemState = IDLE
        │
        ▼
       DONE
```

---

## 21. 總結

此程式目前的核心設計可以簡化為：

```text
Serial Command
      ↓
Command Parser
      ↓
MoveCommand
      ↓
System State
      ↓
MotionRuntime
      ↓
updateMotion()
      ↓
Step Pulse
      ↓
Stepper Motor
```

核心設計理念：

> Serial 負責告訴系統「要做什麼」，`MoveCommand` 負責保存命令，`MotionRuntime` 負責保存執行進度，而 `updateMotion()` 則根據狀態機與時間控制真正產生 Step Pulse。

整體而言，這是一個：

```text
Command Driven
+
State Machine
+
Non-blocking Motion Control
```

的多步進馬達控制架構。
