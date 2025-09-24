#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_mac.h"


#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

// use for debug 
volatile size_t heap_size = 0;

void task_array(void* parameters) {
    while (1) {
        int a = 1;
        int b[100];
        // populate with data so the compiler puts the array in memory
        for (int i = 0; i < 100; i++) {
            b[i] = i + a;
        }
        ESP_LOGI("Task Array", "First element: %d", b[0]);

        // highwater - stack memory remaining in words, free heap (bytes = * 4), total remaining heap left in bytes
        ESP_LOGI("Memory", "mark: %d", uxTaskGetStackHighWaterMark(NULL));
        ESP_LOGI("Memory", "heap: %d", xPortGetFreeHeapSize()); // Memory: heap: 385948

        // allocate memory and then recheck the heap size 
        int *ptr = (int*)pvPortMalloc(1024 * sizeof(int)); // allocate 1024 integers (1024 * 4 = 4096 bytes)
        
        // only allocate if ptr is not null, returns null if it cant allocate memory
        if (ptr != NULL) {

            // assign some shit to this chunk of memory
            // note, this heap memory isn't neccesarily an int[], but it is a chunk of memory and we're just going to put ints in each 4 bytes slot
            // casting ptr tells compiler that this 4096 byte chunk are ints ( all populated with 03)
            for (int i = 0; i < 1024; i++) {
                heap_size = xPortGetFreeHeapSize();
                ptr[i] = 3;
            }
            ESP_LOGI("Memory", "heap: %d", xPortGetFreeHeapSize()); 
        }


       vPortFree(ptr);

        // while loop that mallocs heap every loop, this panics fast
        // ESP_LOGI("Memory", "heap: %d", xPortGetFreeHeapSize()); 

        vTaskDelay(pdMS_TO_TICKS(500));


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

    xTaskCreatePinnedToCore(
        task_array,               
        "Task Array",
        3500,
        NULL,
        1,
        NULL,
        app_cpu
    );
}
