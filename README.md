# FreeRTOS Multitasking & System Health Pipeline (ESP32)

## 📌 Project Overview

This project demonstrates a **real-time multitasking embedded system** built on the **ESP32 dual-core microcontroller** using **FreeRTOS**. It showcases key Real-Time Operating System (RTOS) concepts including task scheduling, interrupt handling, inter-task communication, synchronization, and system health monitoring.

The application is designed to:

- Execute multiple concurrent tasks across both ESP32 cores.
- Handle hardware interrupts efficiently.
- Exchange data safely between tasks using FreeRTOS queues.
- Prevent resource contention using mutexes.
- Monitor system health and recover from task failures using the ESP32 Task Watchdog Timer (TWDT).

---

# System Architecture

The application consists of **four independent FreeRTOS tasks** distributed across the ESP32's dual-core processor.

## Synchronization & Inter-Task Communication

### Binary Semaphore (`buttonSemaphore`)

A binary semaphore connects the **Hardware Interrupt Service Routine (ISR)** to the **Button Task**.

When the ESP32 BOOT button is pressed:

- The ISR executes immediately.
- The semaphore is released.
- The Button Task wakes up without busy waiting.
- Interrupt execution remains extremely short and efficient.

This design keeps ISR execution lightweight while allowing complex processing to occur inside a FreeRTOS task.

---

### Mutex (`serialMutex`)

Since multiple tasks running on different CPU cores share the Serial UART, a mutex protects the resource.

The mutex ensures:

- Only one task prints to Serial at a time.
- UART messages remain readable.
- Multi-core print collisions are prevented.

---

### Queues

Two FreeRTOS queues provide thread-safe communication between tasks.

#### `telemetryQueue`

Transfers system monitoring information from the Telemetry Task to the Monitor Task.

Each queue element contains a structured telemetry packet including:

- System uptime
- Free heap memory
- ADC sensor reading

---

#### `ledBlinkSpeedQueue`

Transfers updated LED blink intervals from the Button Task to the LED Task.

Whenever the user presses the BOOT button, a new blink delay is sent through this queue.

---

# FreeRTOS Task Breakdown

| Task Handle | Task Name | Priority | Core | Responsibility |
|-------------|-----------|----------|------|----------------|
| `hTaskButton` | Button Task | 3 (High) | Core 0 | Waits for button interrupt, debounces input, cycles LED blink speeds, and sends updated delay values to the LED Task. |
| `hTaskTelemetry` | Telemetry Task | 2 (Medium) | Core 1 | Collects system telemetry every second including free heap memory, uptime, and ADC readings, then pushes data into `telemetryQueue`. |
| `hTaskActuator` | LED Task | 1 (Low) | Core 1 | Receives new blink intervals from `ledBlinkSpeedQueue` and toggles the onboard LED (GPIO 2). |
| `hTaskMonitor` | Monitor Task | 1 (Low) | Core 0 | Reads telemetry packets every 3 seconds and prints formatted system diagnostics to the Serial Monitor using a protected UART mutex. |

---

# Task Responsibilities

## Button Task

- Waits indefinitely for the binary semaphore.
- Triggered by the hardware interrupt.
- Performs button debounce.
- Cycles between predefined LED blink speeds.
- Sends the updated blink interval to the LED Task.

---

## Telemetry Task

Runs once every second.

Collects:

- Free heap memory (`esp_get_free_heap_size()`)
- System uptime
- ADC raw reading (GPIO 34)

Packages these values into a `SystemTelemetry` structure and sends it to the Monitor Task.

---

## LED Task

Runs continuously.

Responsibilities:

- Receives updated blink intervals without blocking.
- Changes LED blink rate dynamically.
- Togles the onboard LED connected to GPIO 2.

Supported blink speeds:

- 100 ms
- 300 ms
- 1000 ms

---

## Monitor Task

Runs every three seconds.

Responsibilities:

- Receives telemetry packets.
- Locks the UART mutex.
- Prints formatted system diagnostics.
- Releases the mutex after printing.

Example output:

```text
==============================
System Uptime : 25 s
Free Heap     : 245320 Bytes
ADC Reading   : 1984
LED Delay     : 300 ms
==============================
```

---

# Dual-Core Task Distribution

## Core 0

- Button Task
- Monitor Task

Core 0 primarily handles:

- Interrupt-driven processing
- System monitoring
- User interaction

---

## Core 1

- Telemetry Task
- LED Task

Core 1 handles:

- Periodic sensing
- Actuator control

This separation balances computational load across both ESP32 cores.

---

# Task Watchdog Timer (TWDT)

To improve system reliability, every task registers with the ESP32 **Task Watchdog Timer (TWDT)**.

Configuration:

- Timeout: **5 seconds**

Each task periodically calls:

```cpp
esp_task_wdt_reset();
```

If any registered task:

- hangs,
- blocks indefinitely,
- enters an infinite loop, or
- fails to feed the watchdog,

the ESP32 automatically triggers a hardware reset, allowing the application to recover without manual intervention.

---

# Features Demonstrated

- FreeRTOS multitasking
- Dual-core task scheduling
- Interrupt Service Routine (ISR)
- Binary semaphores
- Mutex synchronization
- Queue-based inter-task communication
- Dynamic LED control
- ADC data acquisition
- Heap memory monitoring
- System uptime tracking
- ESP32 Task Watchdog Timer (TWDT)
- Thread-safe Serial communication

---

# Technologies Used

- ESP32
- FreeRTOS
- Arduino Framework
- ESP-IDF Watchdog APIs
- GPIO Interrupts
- ADC
- UART Serial Communication

---

# Learning Outcomes

This project demonstrates practical implementation of several core embedded systems concepts, including:

- Real-Time Operating Systems (RTOS)
- Deterministic task scheduling
- Inter-task communication (IPC)
- Synchronization primitives
- Multi-core embedded programming
- Fault detection and recovery
- Embedded system health monitoring
- Safe concurrent resource access

It serves as a strong reference project for learning professional FreeRTOS application design on the ESP32 platform.
