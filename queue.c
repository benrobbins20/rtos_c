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


#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

static const uint8_t queue_len = 5;
static QueueHandle_t queue;

void queue_read(void *parameters) {
    int item; // 
    while (1) {
        // copy an item from queue into item, no timeout, do not wait
        // this will pop an item off queue regardless, just queue send
        if (xQueueReceive(queue, (void*) &item, 0) == pdTRUE) {
            // printf("Received item: %d\n", item);
        }
        printf("Item: %d\n", item); // print the last item that xQueueReceive 'placed' into item
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void) {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);

    // serial delay
    vTaskDelay(pdMS_TO_TICKS(5000));

    printf("Queue example\n");

    queue = xQueueCreate(queue_len, sizeof(int));

    xTaskCreatePinnedToCore(
        queue_read, 
        "Queue Read",
        2048,
        NULL,
        1,
        NULL,
        app_cpu
    );

    while (1) {
        static int num = 0; // must be static to avoid initializating variable to 0 everytime
        // try to send num to queue, if queue is full, wait 10 ticks, then try again, then print error if still full
        if (xQueueSend(queue, (void*) &num, 10) != pdTRUE) {
            printf("Queue full\n");
        }
        num++;
        // producer and consumer at 1000ms, never runs into issues
        // vTaskDelay(pdMS_TO_TICKS(1000));

        // producer at 500 and consumer at 1000ms, queue will fill up
        // vTaskDelay(pdMS_TO_TICKS(500));

        // producer at 1500 and consumer at 1000ms, queue will empty, print the last &item
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}
