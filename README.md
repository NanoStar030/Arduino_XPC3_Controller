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
- 使用 `startMotion()` 將 MOVE 命令轉換成實際運動 Runtime

整體架構可分為：

1. 資料結構與參數定義
2. Serial 命令接收
3. 命令解析
4. MOVE 命令參數儲存
5. Motion Runtime 初始化
6. 馬達運動更新
7. 系統狀態管理

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
│   ├── Motion Delay 參數
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
    ├── startMotion()
    ├── updateSerial()
    ├── updateMotion()
    ├── setup()
    └── loop()
```

---

## 3. `main.h` 主要資料結構

### 3.1 Motor

```cpp
struct Motor {
    int stepPin;
    int dirPin;
};
```

每一顆步進馬達由兩個 Pin 控制：

- `stepPin`：每產生一次 Step Pulse，馬達移動一步。
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

`MoveCommand` 用來保存使用者透過 Serial 輸入的 MOVE 命令。

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
| `stepDelayUS` | MOVE 命令傳入的 Step Delay |
| `rampStepNUM` | 加速與減速區間的 Step 數 |

> 注意：目前 `startMotion()` 的實作並沒有使用 `currentMove.stepDelayUS` 作為 `motion.minDelay`，而是直接使用全域設定的 `MIN_DELAY_US`。

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

`MotionRuntime` 儲存馬達運動期間實際使用、且會持續變化的執行資料。

概念上：

```text
MoveCommand
    │
    │ 使用者要求「要怎麼動」
    ▼
startMotion()
    │
    │ 建立實際 Runtime
    ▼
MotionRuntime
    │
    │ 記錄「目前動到哪裡」
    ▼
updateMotion()
```

主要變數：

| 變數 | 說明 |
|---|---|
| `stepNUM` | 總步數 |
| `rampStepNUM` | 加速 / 減速步數 |
| `stepIdx` | 目前已完成的 Step |
| `startDelay` | 起始 Step 間隔 |
| `minDelay` | 最高速度時的最小 Step 間隔 |
| `currentDelay` | 目前使用的 Step 間隔 |
| `delta` | 每一步需要增加或減少的 Delay |
| `lastStepUS` | 上一次 Step 的時間 |

速度和 Delay 的關係：

```text
Delay 越大
    ↓
Step Pulse 頻率越低
    ↓
馬達速度越慢
```

反之：

```text
Delay 越小
    ↓
Step Pulse 頻率越高
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
                  │
                  ▼
                IDLE
                  │
                  │ handleMoveCommand()
                  ▼
              startMotion()
                  │
                  ▼
               RUNNING
               /     \
            STOP      DONE
             │          │
             ▼          │
         STOPPING       │
             │          │
             └────┬─────┘
                  ▼
                 IDLE
```

狀態說明：

| State | 意義 |
|---|---|
| `IDLE` | 系統待機，可以接受新的 MOVE |
| `RUNNING` | 馬達正在運行 |
| `STOPPING` | 收到 STOP，等待 `updateMotion()` 執行停止 |
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
 ├── delay(500)
 └── 印出 "READY"
```

完成初始化後，程式持續進入 `loop()`。

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

這種設計的特色是：

> 不使用長時間 Blocking Loop 控制整段馬達運動。

因此即使馬達正在運行，主程式仍然會持續執行 `updateSerial()`，可以接收例如：

```text
STATUS
STOP
```

等命令。

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

---

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

---

### 8.3 GET_CONFIG

輸入：

```text
GET_CONFIG
```

目前程式會回傳馬達數量與 Delay 相關設定。

程式輸出的格式為：

```text
CONFIG motors_number=<MOTOR_COUNT> start_delau_us=<START_DELAY_US> max_delau_us=<MAX_DELAY_US>
```

其中：

- `MOTOR_COUNT`：馬達數量
- `START_DELAY_US`：運動開始時使用的 Step Delay
- `MAX_DELAY_US`：目前 `GET_CONFIG` 回傳的 Delay 設定值

> 程式目前輸出的欄位名稱使用 `start_delau_us` 與 `max_delau_us`，其中 `delau` 應為程式目前既有的拼字。

---

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

解析命令參數並儲存到 `currentMove`。

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
currentMove 完成
     │
     ▼
startMotion()
     │
     ▼
systemState = RUNNING
```

與舊版不同的是：

```cpp
handleMoveCommand()
```

現在不再直接設定：

```cpp
systemState = SystemState::RUNNING;
```

而是呼叫：

```cpp
startMotion();
```

由 `startMotion()` 完成 Runtime 初始化、方向設定與狀態切換。

---

## 11. `startMotion()`

`startMotion()` 是目前 MOVE 命令與實際馬達運動之間的重要初始化層。

目前程式：

```cpp
void startMotion() {
    motion.stepNUM = currentMove.stepNUM;
    motion.rampStepNUM  = currentMove.rampStepNUM;

    motion.stepIdx = 0;
    motion.startDelay = START_DELAY_US;
    motion.minDelay   = MIN_DELAY_US;

    motion.delta = (motion.startDelay - motion.minDelay) / motion.rampStepNUM;

    if (motion.delta < 1)
        motion.delta = 1;

    motion.currentDelay = motion.startDelay;
    motion.lastStepUS = micros();

    digitalWrite(
        motors[currentMove.motorIdx].dirPin,
        currentMove.dir == Direction::POSITIVE
    );

    systemState = SystemState::RUNNING;
}
```

### 11.1 複製 MoveCommand 參數

首先將命令中的運動資料複製到 Runtime：

```cpp
motion.stepNUM = currentMove.stepNUM;
motion.rampStepNUM = currentMove.rampStepNUM;
```

因此：

```text
currentMove.stepNUM
        │
        ▼
 motion.stepNUM

currentMove.rampStepNUM
        │
        ▼
 motion.rampStepNUM
```

---

### 11.2 重設運動進度

```cpp
motion.stepIdx = 0;
```

每次新的 MOVE 開始時，Step 計數器都從 0 開始。

---

### 11.3 設定起始與最小 Delay

```cpp
motion.startDelay = START_DELAY_US;
motion.minDelay   = MIN_DELAY_US;
```

其中：

- `startDelay`：馬達剛開始運行時的 Step 間隔
- `minDelay`：加速後允許使用的最小 Step 間隔

一般而言：

```text
startDelay > minDelay
```

因此馬達可以從較慢的 Step Frequency 逐漸加速到較高的 Step Frequency。

---

### 11.4 計算 Ramp Delta

```cpp
motion.delta =
    (motion.startDelay - motion.minDelay)
    / motion.rampStepNUM;
```

`delta` 表示每走一步時，`currentDelay` 應該調整多少。

概念：

```text
Delay
 ▲
 │ startDelay
 │     \
 │      \
 │       \
 │        \ minDelay
 └──────────────────► Step
      rampStepNUM
```

若計算結果小於 1：

```cpp
if (motion.delta < 1)
    motion.delta = 1;
```

則至少使用 `1 us` 的 Delay 變化量。

---

### 11.5 設定目前 Delay

```cpp
motion.currentDelay = motion.startDelay;
```

新的 Motion 一開始會從 `startDelay` 開始。

接著 `updateMotion()` 才會逐步降低 `currentDelay` 進行加速。

---

### 11.6 初始化 Step Timer

```cpp
motion.lastStepUS = micros();
```

記錄 Motion 啟動時間。

之後 `updateMotion()` 使用：

```cpp
now - motion.lastStepUS
```

判斷是否已經到下一個 Step 的時間。

---

### 11.7 設定馬達方向

```cpp
digitalWrite(
    motors[currentMove.motorIdx].dirPin,
    currentMove.dir == Direction::POSITIVE
);
```

邏輯為：

```text
Direction::POSITIVE
        │
        ▼
    DIR Pin HIGH

Direction::NEGATIVE
        │
        ▼
    DIR Pin LOW
```

因此方向會在正式進入 `RUNNING` 之前設定完成。

---

### 11.8 切換系統狀態

最後：

```cpp
systemState = SystemState::RUNNING;
```

表示 Runtime 已初始化完成，可以開始由 `updateMotion()` 產生 Step Pulse。

完整流程：

```text
currentMove
    │
    ▼
startMotion()
    │
    ├── 複製 stepNUM
    ├── 複製 rampStepNUM
    ├── stepIdx = 0
    ├── 設定 startDelay
    ├── 設定 minDelay
    ├── 計算 delta
    ├── currentDelay = startDelay
    ├── lastStepUS = micros()
    ├── 設定 DIR Pin
    └── systemState = RUNNING
```

---

## 12. 馬達 Step Pulse

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

## 13. `updateMotion()`

`updateMotion()` 是實際執行馬達運動的核心。

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

## 14. 非阻塞式 Step Timing

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

如果尚未到下一個 Step 的時間，函式直接 `return`。

因此程式不需要用長時間 `delay()` 阻塞整個主迴圈。

---

## 15. 加速 / 定速 / 減速

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

### 15.1 Acceleration

條件：

```cpp
motion.stepIdx < motion.rampStepNUM
```

程式會逐步執行：

```cpp
motion.currentDelay -= motion.delta;
```

Delay 減少代表 Step Frequency 增加，因此馬達逐漸加速。

若 Delay 接近 `minDelay`：

```cpp
motion.currentDelay = motion.minDelay;
```

避免繼續降低。

---

### 15.2 Constant Speed

如果目前 Step 不在加速或減速區：

```cpp
motion.currentDelay = motion.minDelay;
```

因此馬達維持固定的 Step Interval。

---

### 15.3 Deceleration

條件：

```cpp
motion.stepIdx >= motion.stepNUM - motion.rampStepNUM
```

程式執行：

```cpp
motion.currentDelay += motion.delta;
```

Delay 增加代表 Step Frequency 降低，因此馬達逐漸減速。

如果超過起始 Delay：

```cpp
motion.currentDelay = motion.startDelay;
```

避免 Delay 繼續增加。

---

## 16. 運動完成

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

## 17. 完整資料流

目前完整 MOVE 資料流為：

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
            ▼
    handleMoveCommand()
            │
            ▼
       currentMove
            │
            ▼
       startMotion()
            │
      ┌─────┼───────────────┐
      │     │               │
      │     │               └── 設定 DIR Pin
      │     │
      │     └── 初始化 MotionRuntime
      │
      └── systemState = RUNNING
            │
            ▼
       updateMotion()
            │
      ┌─────┼────────────┐
      │     │            │
    Accel Constant     Decel
      │     │            │
      └─────┼────────────┘
            │
            ▼
      OneStepPulse()
            │
            ▼
          Motor
```

---

## 18. System State Flow

```text
                 Arduino Boot
                      │
                      ▼
                    IDLE
                      │
                      │ MOVE
                      ▼
             handleMoveCommand()
                      │
                      ▼
                 startMotion()
                      │
                      ▼
                   RUNNING
                   /     \
                  /       \
               STOP       DONE
                │           │
                ▼           │
            STOPPING        │
                │           │
                └─────┬─────┘
                      ▼
                    IDLE
```

---

## 19. 程式架構特色

### 19.1 Serial 與 Motion 分離

程式將不同責任拆開：

```text
updateSerial()
      │
      └── 收 Serial 資料

handleCommand()
      │
      └── 判斷命令種類

handleMoveCommand()
      │
      └── 解析 MOVE 參數

startMotion()
      │
      └── 初始化 Runtime 與硬體方向

updateMotion()
      │
      └── 非阻塞地執行 Step Motion
```

這使每一個函式都有比較明確的責任。

---

### 19.2 Command 與 Runtime 分開

```text
MoveCommand
    =
使用者輸入的命令資料

MotionRuntime
    =
執行 Motion 時需要的即時狀態
```

而 `startMotion()` 就是兩者之間的轉換層：

```text
MoveCommand
    │
    ▼
startMotion()
    │
    ▼
MotionRuntime
```

---

### 19.3 使用 State Machine

透過 `SystemState` 統一管理：

```text
IDLE
RUNNING
STOPPING
ERROR
```

可以避免系統在馬達運行期間接受第二個 MOVE 命令。

---

### 19.4 非阻塞式運動控制

`updateMotion()` 透過 `micros()` 判斷下一個 Step 的時間。

因此除了單次 `OneStepPulse()` 中很短的 `delayMicroseconds(3)` 之外，不需要使用長時間 Blocking Delay 控制完整運動。

這讓主迴圈仍然可以持續處理 Serial。

---

## 20. 目前程式需要注意的地方

### 20.1 `stepDelayUS` 目前尚未套用到 Motion Runtime

MOVE 命令仍然會解析：

```cpp
currentMove.stepDelayUS
```

但是目前 `startMotion()` 使用：

```cpp
motion.minDelay = MIN_DELAY_US;
```

並沒有使用：

```cpp
currentMove.stepDelayUS
```

因此目前不同 MOVE 命令即使傳入不同的 `stepDelayUS`，實際最高速度仍由 `MIN_DELAY_US` 決定。

目前資料流實際是：

```text
MOVE stepDelayUS
      │
      ▼
currentMove.stepDelayUS
      │
      └── 目前未進入 motion.minDelay
```

---

### 20.2 `rampStepNUM` 需要大於 0

`startMotion()` 中：

```cpp
motion.delta =
    (motion.startDelay - motion.minDelay)
    / motion.rampStepNUM;
```

因此 `motion.rampStepNUM` 若為 0，會造成除以零問題。

目前 `handleMoveCommand()` 只負責解析 `rampStepNUM`，尚未看到對 0 的檢查。

---

### 20.3 Ramp 長度應與總步數保持合理關係

`updateMotion()` 使用：

```cpp
motion.stepIdx < motion.rampStepNUM
```

判斷加速區，並使用：

```cpp
motion.stepIdx >= motion.stepNUM - motion.rampStepNUM
```

判斷減速區。

因此實際使用時，通常應確保 Ramp 長度不會超過整段 Motion 可合理容納的範圍。

---

### 20.4 `GET_CONFIG` 的輸出名稱

目前程式使用：

```text
start_delau_us
max_delau_us
```

如果 Serial protocol 後續會由 PC 程式解析，建議固定欄位名稱，避免未來修改造成相容性問題。

---

## 21. 完整 MOVE 運行流程

```text
Serial MOVE Command
        │
        ▼
handleCommand()
        │
        ├── 確認 systemState == IDLE
        │
        ▼
handleMoveCommand()
        │
        ├── Parse Motor
        ├── Validate Motor
        ├── Parse Direction
        ├── Validate Direction
        ├── Parse Steps
        ├── Parse Step Delay
        └── Parse Ramp Steps
        │
        ▼
currentMove
        │
        ▼
startMotion()
        │
        ├── motion.stepNUM = currentMove.stepNUM
        ├── motion.rampStepNUM = currentMove.rampStepNUM
        ├── motion.stepIdx = 0
        ├── motion.startDelay = START_DELAY_US
        ├── motion.minDelay = MIN_DELAY_US
        ├── 計算 motion.delta
        ├── motion.currentDelay = motion.startDelay
        ├── motion.lastStepUS = micros()
        ├── 設定 DIR Pin
        └── systemState = RUNNING
        │
        ▼
updateMotion()
        │
        ├── 檢查 STOPPING
        ├── 檢查 Step Timing
        ├── OneStepPulse()
        ├── stepIdx++
        ├── Acceleration
        ├── Constant Speed
        └── Deceleration
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

## 22. 總結

目前程式的核心架構可以簡化為：

```text
Serial Command
      ↓
updateSerial()
      ↓
handleCommand()
      ↓
handleMoveCommand()
      ↓
MoveCommand
      ↓
startMotion()
      ↓
MotionRuntime
      ↓
SystemState = RUNNING
      ↓
updateMotion()
      ↓
Step Pulse
      ↓
Stepper Motor
```

核心設計理念為：

> Serial 負責接收「要做什麼」，`MoveCommand` 保存使用者的命令，`startMotion()` 將命令轉換成實際運動所需的 `MotionRuntime`，而 `updateMotion()` 則根據 Runtime、狀態機與時間控制真正產生 Step Pulse。

整體而言，目前架構屬於：

```text
Command Driven
+
State Machine
+
Motion Initialization Layer
+
Non-blocking Motion Control
```

相較於先前版本，新增的 `startMotion()` 已經補上了 MOVE 命令與 `updateMotion()` 之間的初始化流程，使整體責任分工更加清楚。
