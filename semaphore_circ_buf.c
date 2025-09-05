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

// using enum is a somewhat universial way to define constants
enum {
    BUF_SIZE = 5
};
static const int prod_tasks = 5;
static const int cons_tasks = 2;
static const int num_writes = 3;

static int buf[BUF_SIZE]; // circ buffer
// consumer -> read tail, producer -> write head, two pointer style
static int head = 0;
static int tail = 0;
static SemaphoreHandle_t bin_sem = NULL; // bin sem to lock task parameter access

// simulate resource sharing by marking a slot as filled or as empty (it will always be filled but values are overwritten), use bin_sem to lock serial output
static SemaphoreHandle_t empty_slots;
static SemaphoreHandle_t filled_slots;
static SemaphoreHandle_t print_mutex;

// you can also just push and pop to a queue...
static QueueHandle_t task_queue;
static const int queue_len = BUF_SIZE;





// producer/consumer tasks
void producer(void* pvParameters) {
    // cast parameter to int pointer and deref, release bin_sem after parameter is read in
    int num = *(int *)pvParameters; 
    xSemaphoreGive(bin_sem);

    // wait for an empty slot, then write num to buf
    for (int i = 0; i < num_writes; i++) {
        // using semaphores
        // xSemaphoreTake(empty_slots, portMAX_DELAY); // wait for an empty slot
        // xSemaphoreTake(print_mutex, portMAX_DELAY); // lock serial output while writing

        // using a queue
        xQueueSend(task_queue, (void*)&num, portMAX_DELAY);

        // buf[head] = num;
        // head = (head + 1) % BUF_SIZE; // mod to wrap around (circular)
        // xSemaphoreGive(print_mutex); // release serial output lock
        // xSemaphoreGive(filled_slots); // mark a filled slot
    }
    vTaskDelete(NULL);
}

void consumer(void* pvParameters) {
    int val;
    
    while(1) {
        
        // xSemaphoreTake(filled_slots, portMAX_DELAY); // wait for filled slot
        // 

        // wait for items in queue
        xQueueReceive(task_queue, (void*)&val, portMAX_DELAY);
        // val = buf[tail];
        // tail = (tail + 1) % BUF_SIZE;

        xSemaphoreTake(print_mutex, portMAX_DELAY); // wait for serial lock
        ESP_LOGI("Producer", "Value: %d", val);
        xSemaphoreGive(print_mutex); // release serial lock
        
        // xSemaphoreGive(empty_slots); // mark an empty slot
        vTaskDelay(pdMS_TO_TICKS(10)); // yield?
    }
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

    // create semaphores for slots and mutex for printing
    empty_slots = xSemaphoreCreateCounting(BUF_SIZE, BUF_SIZE); // initially empty, 5 open slots
    filled_slots = xSemaphoreCreateCounting(BUF_SIZE, 0); // 0 filled slots
    print_mutex = xSemaphoreCreateMutex();

    // queue method
    task_queue = xQueueCreate(queue_len, sizeof(int));

    char task_name[30];
    bin_sem = xSemaphoreCreateBinary(); // tasks parameter lock
    // create each producer task and wait for it to read in param and release bin_sem
    for (int i = 0; i < prod_tasks; i++) {
        sprintf(task_name, "producer_%d", i);
        xTaskCreatePinnedToCore(producer, 
            task_name, 
            4096, 
            (void*)&i, // pass the iterator variable because each task will print its own task number (0-4)
            1, 
            NULL, 
            app_cpu);
        xSemaphoreTake(bin_sem, portMAX_DELAY);
    }

    // create each consumer task, no need to lock main, no params
    for (int i = 0; i < cons_tasks; i++) {
        sprintf(task_name, "consumer_%d", i);
        xTaskCreatePinnedToCore(consumer, 
            task_name, 
            4096, 
            NULL, 
            1, 
            NULL, 
            app_cpu);
    }

    // lock serial mutex to print
    xSemaphoreTake(print_mutex, portMAX_DELAY);
    ESP_LOGI("Main", "All tasks created");
    xSemaphoreGive(print_mutex);
}