
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
#include "esp_random.h"
#include "freertos/semphr.h"


#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

static int global_counter = 0;

// semaphore handle (for mutex)
static SemaphoreHandle_t mutex = NULL;


// single rtos function with 2 rtos tasks
void increment_counter(void *parameters) {

    // local var to spend some instructions spinning
    int local_counter = 0;
    while (1) {

        // set up task logging
        TaskHandle_t task = xTaskGetCurrentTaskHandle();
        TaskStatus_t status;
        vTaskGetInfo(task, &status, pdTRUE, eInvalid);
        
        // vary the delay
        volatile uint32_t rand = esp_random();
        volatile uint32_t delay = 100 + (rand % 1901); // 0-900 ~ % 901 = n slots, 100 - 900 = 100 + rand % 801
        ESP_LOGI("delay", "delay: %d", (unsigned int)delay);
        vTaskDelay(pdMS_TO_TICKS(delay));

        // lock the mutex before copying the shared variable
        if (xSemaphoreTake(mutex, 0) == pdTRUE) {
            
            // %%%%%%%%%%%% CRITICAL SECTION %%%%%%%%%%%%
            local_counter = global_counter;
            local_counter++;
            global_counter = local_counter;
            // %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%



            // can release lock in the same if block apparently
            xSemaphoreGive(mutex);
        }

    
        else {
            ESP_LOGE("mutex", "mutex %p not taken by %s", (void *)mutex, status.pcTaskName);
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

    vTaskDelay(pdMS_TO_TICKS(3000)); 

    // use mutex for variable sharing
    mutex = xSemaphoreCreateMutex();
    
    xTaskCreatePinnedToCore(
        increment_counter, 
        "ic1",
        4096,
        NULL,
        1,
        NULL,
        app_cpu
    );

     
    // race condition without variable locking 
    xTaskCreatePinnedToCore(
        increment_counter, 
        "ic2",
        4096,
        NULL,
        1,
        NULL,
        app_cpu
    );

    vTaskDelete(NULL);
}
