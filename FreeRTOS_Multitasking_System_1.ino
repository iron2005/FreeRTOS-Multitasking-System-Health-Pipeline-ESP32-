#include <Arduino.h>
#include "esp_task_wdt.h"

// ============================================================================
// HARDWARE DEFINITIONS
// ============================================================================
#define BOOT_BUTTON_PIN   0       // Built-in ESP32 BOOT Button
#define LED_PIN           2       // Built-in Blue LED on most ESP32 DevKits
#define ANALOG_INPUT_PIN  34      // ADC Pin reading floating voltage/noise
#define WDT_TIMEOUT_SEC   5

// Data Structure for Telemetry
struct SystemTelemetry {
    uint32_t freeHeapBytes;      // Real available RAM
    uint32_t systemUptimeSec;    // Real uptime in seconds
    int      rawAnalogValue;     // Real ADC reading
};

// ============================================================================
// FREERTOS HANDLES
// ============================================================================
QueueHandle_t      telemetryQueue;
QueueHandle_t      ledBlinkSpeedQueue; // Controls LED speed via Button
SemaphoreHandle_t  serialMutex;
SemaphoreHandle_t  buttonSemaphore;   // Triggered by hardware interrupt

TaskHandle_t       hTaskButton;
TaskHandle_t       hTaskTelemetry;
TaskHandle_t       hTaskActuator;
TaskHandle_t       hTaskMonitor;

// ============================================================================
// HARDWARE INTERRUPT SERVICE ROUTINE (ISR)
// ============================================================================
// Triggered when BOOT button is pressed
void IRAM_ATTR buttonISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // Signal the Button Task from ISR safely
    xSemaphoreGiveFromISR(buttonSemaphore, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    while (!Serial) { delay(10); }

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);

    // Attach Hardware Interrupt to BOOT Button (Falling Edge = Press)
    attachInterrupt(digitalPinToInterrupt(BOOT_BUTTON_PIN), buttonISR, FALLING);

    // Initialize FreeRTOS Queues & Mutexes
    serialMutex         = xSemaphoreCreateMutex();
    buttonSemaphore     = xSemaphoreCreateBinary();
    telemetryQueue      = xQueueCreate(5, sizeof(SystemTelemetry));
    ledBlinkSpeedQueue  = xQueueCreate(5, sizeof(uint32_t));

    // Watchdog Configuration
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&twdt_config);

    // Create Real Tasks
    xTaskCreatePinnedToCore(TaskButtonHandler,  "ButtonTask",  2048, NULL, 3, &hTaskButton,    0);
    xTaskCreatePinnedToCore(TaskRealTelemetry,  "TelemTask",   3072, NULL, 2, &hTaskTelemetry, 1);
    xTaskCreatePinnedToCore(TaskLEDActuator,    "LEDTask",     2048, NULL, 1, &hTaskActuator,  1);
    xTaskCreatePinnedToCore(TaskSystemMonitor,  "MonitorTask", 3072, NULL, 1, &hTaskMonitor,   0);

    Serial.println("\n[SYSTEM] Real-Hardware FreeRTOS Pipeline Online.");
    Serial.println("[HINT] Press the physical BOOT button on ESP32 to change LED speed!\n");
}

void loop() {
    vTaskDelete(NULL); // Free memory, setup complete
}

// ============================================================================
// TASK 1: BUTTON INTERRUPT HANDLER (Debounces and toggles LED speed)
// ============================================================================
void TaskButtonHandler(void *pvParameters) {
    esp_task_wdt_add(NULL);
    uint32_t blinkDelays[] = {100, 300, 1000}; // Speed levels in ms
    uint8_t currentSpeedIndex = 0;

    while (1) {
        esp_task_wdt_reset();

        // Wait efficiently for ISR signal (Zero CPU consumption while sleeping)
        if (xSemaphoreTake(buttonSemaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
            
            // Simple Debounce delay
            vTaskDelay(pdMS_TO_TICKS(150)); 

            currentSpeedIndex = (currentSpeedIndex + 1) % 3;
            uint32_t newDelay = blinkDelays[currentSpeedIndex];

            // Send new delay speed to LED Task
            xQueueSend(ledBlinkSpeedQueue, &newDelay, 0);

            if (xSemaphoreTake(serialMutex, portMAX_DELAY)) {
                Serial.printf("\n>>> [HARDWARE INTERRUPT] BOOT Button Pressed! LED Speed set to: %u ms <<<\n\n", newDelay);
                xSemaphoreGive(serialMutex);
            }
        }
    }
    
}

// ============================================================================
// TASK 2: REAL TELEMETRY SAMPLER (Reads Heap & ADC)
// ============================================================================
void TaskRealTelemetry(void *pvParameters) {
    esp_task_wdt_add(NULL);
    SystemTelemetry packet;

    while (1) {
        esp_task_wdt_reset();

        // Query real ESP32 silicon status
        packet.freeHeapBytes   = esp_get_free_heap_size();
        packet.systemUptimeSec = millis() / 1000;
        packet.rawAnalogValue  = analogRead(ANALOG_INPUT_PIN);

        // Push real metrics to queue
        xQueueSend(telemetryQueue, &packet, pdMS_TO_TICKS(100));

        vTaskDelay(pdMS_TO_TICKS(1000)); // Sample every 1 sec
    }
}

// ============================================================================
// TASK 3: ACTUATOR CONTROL (Drives Physical LED)
// ============================================================================
void TaskLEDActuator(void *pvParameters) {
    esp_task_wdt_add(NULL);
    uint32_t toggleDelayMs = 500; // Default speed
    uint32_t updatedDelay = 0;

    while (1) {
        esp_task_wdt_reset();

        // Check if button task requested speed change (Non-blocking)
        if (xQueueReceive(ledBlinkSpeedQueue, &updatedDelay, 0) == pdTRUE) {
            toggleDelayMs = updatedDelay;
        }

        // Drive real physical GPIO
        digitalWrite(LED_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(toggleDelayMs));
        
        digitalWrite(LED_PIN, LOW);
        vTaskDelay(pdMS_TO_TICKS(toggleDelayMs));
    }
}

// ============================================================================
// TASK 4: SYSTEM HEALTH & METRICS MONITOR
// ============================================================================
void TaskSystemMonitor(void *pvParameters) {
    esp_task_wdt_add(NULL);
    SystemTelemetry rxPacket;

    while (1) {
        esp_task_wdt_reset();

        // Print Telemetry when it arrives in Queue
        if (xQueueReceive(telemetryQueue, &rxPacket, pdMS_TO_TICKS(2000)) == pdTRUE) {
            if (xSemaphoreTake(serialMutex, portMAX_DELAY)) {
                Serial.printf("[TELEMETRY] Uptime: %us | Free RAM: %u Bytes | Pin 34 ADC: %d\n",
                              rxPacket.systemUptimeSec,
                              rxPacket.freeHeapBytes,
                              rxPacket.rawAnalogValue);
                xSemaphoreGive(serialMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}