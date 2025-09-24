
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/queue.h"
#include "esp_vfs_dev.h"


#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

static const uint8_t queue_len = 5;
static const uint8_t buffer_len = 255;
static QueueHandle_t queue1;
static QueueHandle_t queue2;
static const int led_pin = 21;

// initialized struct for message to send back to queue2
typedef struct {
    int led_counter;
    char message[20];
} LedMessage;


void printBuf(char* buf, uint8_t len) {
    for (int i = 0; i < len; i++) {
        char buf_char = buf[i];
        ESP_LOGI("TAG", "char: %c, addr: %p", buf_char, (void*)&buf[i]); // print the address, should be consequetive bytes
        for (int bit = 7; bit >= 0; bit--) {
            printf("%d", (buf_char >> bit) & 1);
        }
        printf("\n");
    }
}

void queue1_task(void *parameters) {
    int c;
    char serial_buf[buffer_len];
    memset(serial_buf, 0, buffer_len);
    uint32_t idx = 0;

    while (1) {
        c = getchar();

        // keep reading queue2 for the ledmessage
        if (queue2 != NULL) {
            LedMessage led_msg;
            if (xQueueReceive(queue2, &led_msg, 0) == pdTRUE) {
                ESP_LOGI("task1", "Received LedMessage: %d blinks", led_msg.led_counter);
                printf("%s%d\n", led_msg.message, led_msg.led_counter);
            } 
            else {
                vTaskDelay(pdMS_TO_TICKS(10)); // WDT, 'yield' to other tasks when no message
            }
        }

        // read serial    
        if (c != EOF) {
            if (c == '\r' || c == '\n') {
                if (idx > 0) {
                    serial_buf[idx] = '\0';

                    char prefix[7] = "delay ";
                    if (strncmp(serial_buf, prefix, 6) == 0) {
                        int delay_ms = atoi(serial_buf + 6);
                        ESP_LOGI("task1", "Delay command received: %d ms", delay_ms);


                        if (queue1 != NULL) {
                            if (xQueueSend(queue1, (void *)&delay_ms, 0) == pdTRUE) {
                                ESP_LOGI("task1", "Sent %d ms to queue1", delay_ms);
                            } 
                            else {
                                ESP_LOGE("task1", "Failed to send to queue1");
                                vTaskDelay(pdMS_TO_TICKS(100)); // WDT on the queue being full, or just idle...?
                            }
                        }
                    } 
                    // echo back to console
                    else {
                        printf("%s\n", serial_buf);
                    }

                    memset(serial_buf, 0, buffer_len);
                    idx = 0;
                }
            } 
            else if (idx < buffer_len - 1) {
                serial_buf[idx++] = (char)c;
            }
        } 
        
        else {
            vTaskDelay(pdMS_TO_TICKS(10)); // WDT
        }
    }
}

void queue2_task(void *parameteres) {
    int delay_ms = 500; // default delay
    int counter = 0; // local counter to assign to LedMessage

    // initialize LedMessage struct
    LedMessage led_msg = {
        .led_counter = 0,
        .message = "Blinks: "
    };

    // read from queue1, return immediately if no message
    while (1) {
        if (xQueueReceive(queue1, &delay_ms, 0) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(10)); // WDT, 'yield' to other tasks when no message
        }
        ESP_LOGI("task2", "Toggling LED with delay: %d ms", delay_ms);
        gpio_set_level(led_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        gpio_set_level(led_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        counter++;
        if (counter >= 10) {
            led_msg.led_counter = counter;
            xQueueSend(queue2, (void *)&led_msg, 0);
            ESP_LOGI("task2", "Sent LedMessage to queue2: %d blinks", led_msg.led_counter);
            counter = 0; // reset counter after sending
        }
    }
}

void app_main(void) {
    // must use uart 0 for usb serial stuff, 
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);

    // led pin setup 
    gpio_reset_pin(led_pin);
    gpio_set_direction(led_pin, GPIO_MODE_OUTPUT);

    // serial delay
    vTaskDelay(pdMS_TO_TICKS(5000));

    queue1 = xQueueCreate(queue_len, sizeof(int));
    queue2 = xQueueCreate(queue_len, sizeof(LedMessage));

    xTaskCreatePinnedToCore(
        queue1_task, 
        "Queue Task 1",
        4096,
        NULL,
        1,
        NULL,
        app_cpu
    );
    xTaskCreatePinnedToCore(
        queue2_task, 
        "Queue Task 2",
        4096,
        NULL,
        1,
        NULL,
        app_cpu
    );
}
