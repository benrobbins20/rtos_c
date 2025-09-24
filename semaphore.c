/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

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
 #include "driver/usb_serial_jtag.h"

#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

typedef struct Message {
    char body[20];
    uint8_t len;
} Message;

// counting semaphore for taking and releases resources
static SemaphoreHandle_t sem_params = NULL;

void tasks(void *pvParameters) {
    // cast pvparams to Message pointer and dereference
    Message msg = *(Message *) pvParameters;
    // give the semaphore because we are done using a shared reference to msg
    xSemaphoreGive(sem_params);

    ESP_LOGI("Task", "Msg body: %s", msg.body);
    ESP_LOGI("Task", "Msg len: %d", msg.len);

    // delete task
    vTaskDelay(pdMS_TO_TICKS(1000));
    vTaskDelete(NULL);
}

void app_main(void) {
    // configure console settings and wait
    usb_serial_jtag_driver_config_t serial_cfg = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 1024
    };
    usb_serial_jtag_driver_install(&serial_cfg);
    esp_vfs_usb_serial_jtag_use_driver();
    vTaskDelay(pdMS_TO_TICKS(5000));

    // create a counting semaphore for the task count
    sem_params = xSemaphoreCreateCounting(5, 0);

    char task_name[20];
    char msg_body[20];
    Message msg;
    strcpy(msg.body, "Hello from task");
    msg.len = strlen(msg.body);

    // start 5 tasks
    for (int i = 0; i < 5; i++) {
        ESP_LOGI("Task_loop", "Task: %d", i);
        // write task name
        sprintf(task_name, "Task_%d", i);
        xTaskCreatePinnedToCore(
            tasks,
            task_name,
            4096,
            (void*)&msg,
            1,
            NULL,
            app_cpu
        );
    }

    // take 5 semaphores that the tasks will give back once they are done accessing msg
    for (int i = 0; i < 5; i++) {
        xSemaphoreTake(sem_params, portMAX_DELAY);
    }

    ESP_LOGI("Main", "All tasks created");

    
}