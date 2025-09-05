#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

static const uint8_t buf_len = 20;
static const int led_pin = 21;
static int led_delay = 500;

// prints the uart receive buffer in ascii
// "5000\r\n"
// 
void printBuf(char* buf, uint8_t len) {
    for (int i = 0; i < len; i++) {
        char buf_char = buf[i];
        ESP_LOGI("TAG", "char: %c = ", buf_char);
        for (int bit = 7; bit >= 0; bit--) {
            printf("%d", (buf_char >> bit) & 1);
        }
        printf("\n");
    }
}
void toggle_led(void* parameters) {
    while (1) {
        ESP_LOGI("TAG", "Toggling LED with delay: %d ms", led_delay);
        gpio_set_level(led_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(led_delay));
        gpio_set_level(led_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(led_delay));
    }
}
void print_task_info() {
    TaskStatus_t xTaskDetails;
    // NULL gets info for the current task
    vTaskGetInfo(NULL, &xTaskDetails, pdTRUE, eInvalid);
    ESP_LOGI("TAG", "Task Name: %s", xTaskDetails.pcTaskName);
    ESP_LOGI("TAG", "Task State: %d", xTaskDetails.eCurrentState);
    ESP_LOGI("TAG", "Task Priority: %u", xTaskDetails.uxCurrentPriority);
    ESP_LOGI("TAG", "Stack High Water Mark: %lu", (unsigned long)xTaskDetails.usStackHighWaterMark);
    ESP_LOGI("TAG", "Running on Core: %d", xPortGetCoreID());}

void read_serial(void* parameters) {
    // 
    int c;
    char buf[20] = {0};
    uint8_t idx = 0;

    while (1) {
        c = getchar();  // Read from USB_SERIAL_JTAG
        if (c != EOF) {
            if (c == '\r' || c == '\n') {
                if (idx > 0) {
                    buf[idx] = '\0';
                    led_delay = atoi(buf);
                    ESP_LOGI("TAG", "Received delay: %d ms", led_delay);
                    printBuf(buf, idx);
                    idx = 0;
                }
            } else if (idx < buf_len - 1) {
                buf[idx++] = (char)c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void app_main(void) {
    // Initialize LED GPIO
    gpio_reset_pin(led_pin);
    gpio_set_direction(led_pin, GPIO_MODE_OUTPUT);
    ESP_LOGI("TAG", "Waiting 5 seconds before printing task info...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    // Create LED toggle task
    xTaskCreatePinnedToCore(
        toggle_led,
        "toggle_led",
        2048,
        NULL,
        1,
        NULL,
        app_cpu
    );
    // Create serial input task
    xTaskCreatePinnedToCore(
        read_serial,
        "read_serial",
        4096,
        NULL,
        1,
        NULL,
        app_cpu
    );
    // Print task info
    // print_task_info();
    ESP_LOGI("TAG", "Application started on core %d", xPortGetCoreID());
}