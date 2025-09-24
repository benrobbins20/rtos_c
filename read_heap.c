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

// buffer length for serial input
static const uint8_t buf_len = 255;

// global mutable pointer and status flag
static volatile uint8_t msg_flag = 0; // flag will change between rtos tasks, must be volatile data
static char *msg_ptr = NULL; // pointer to first char of message

// 
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

void read_serial(void* parameters) {
    // 
    int c;
    char buf[buf_len];
    memset(buf, 0, buf_len);
    uint32_t idx = 0;

    while (1) {
        c = getchar();
        if (c != EOF) { // IMPORTANT: 
            // newline received, print the stack buffer
            // allocate heap memory and copy over buffer 
            if (c == '\r' || c == '\n') {
                if (idx > 0) {
                    buf[idx] = '\0';
                    ESP_LOGI("TAG", "Message from stack buffer: ");
                    printBuf(buf, idx);
                    
                    // check the status flag, assert heap pointer assigned
                    if (msg_flag == 0) {
                        msg_ptr = (char*) pvPortMalloc((idx + 1) * sizeof(char));
                        configASSERT(msg_ptr != NULL);

                        // write buffer to heap, set flag
                        memcpy(msg_ptr, buf, idx + 1);
                        msg_ptr[idx] = '\0';
                        msg_flag = 1;
                    }
                    
                    // memory is written, reset stack buffer/index
                    memset(buf, 0, buf_len);
                    idx = 0;
                }
            }


            // write char to buffer
            else if (idx < buf_len - 1) {
                buf[idx++] = (char)c;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // WDT
    }
}

void read_heap(void* parameters) {
    while (1) {
        if (msg_flag == 1) {

            // can just print the whole null-term string
            // ESP_LOGI("TAG", "Message from heap: %s", msg_ptr);

            // use function to print the buffer
            ESP_LOGI("TAG", "Message from heap:");
            printBuf(msg_ptr, strlen(msg_ptr));

            vPortFree(msg_ptr);
            msg_ptr = NULL; 
            msg_flag = 0;
        }
    vTaskDelay(pdMS_TO_TICKS(10)); // WDT
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
        read_serial,               
        "read serial",
        3500,
        NULL,
        1,
        NULL,
        app_cpu
    );

    xTaskCreatePinnedToCore(
        read_heap,
        "read heap",
        3500,
        NULL,
        2,
        NULL,
        app_cpu
    );
}
