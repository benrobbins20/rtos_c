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
#include "driver/timer.h"
#include "freertos/timers.h"
#include "driver/gptimer.h"
#include "esp_adc/adc_oneshot.h"

#define NUM_TASKS 5

#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

static SemaphoreHandle_t args_sem; // purely to lock main until params have been passed to tasks
static SemaphoreHandle_t done_eating; // notify main
static SemaphoreHandle_t chopstick[5]; // 5 chop sticks for 5 philosophers
static SemaphoreHandle_t waiter; // waiter allows philosophers to eat one at a time (binary sem)

// single philosophers task that attempts to pick up left chop stick and right chopstick and eat a meal
// handle concurrency
void philophers(void *args) {
    int num = *(int *)args; // int passed to each task to choose a chopstick semaphore
    int next = (num+1)%NUM_TASKS; // get next chopstick with rollover
    xSemaphoreGive(args_sem); // release semaphore after main provides num param

    // you can use hierarchy OR arbitrator (waiter)
    // but it works with both! Arbitrator may technically be slower.

    // hierarchy with lowest being the highest priority
    int idx1 = num;
    int idx2 = next;
    if (num < next) {
        idx1 = num;
        idx2 = next;
    }
    else {
        idx1 = next;
        idx2 = num;
    }

    // inject some random delay before any mutex takes 
    vTaskDelay(pdMS_TO_TICKS(esp_random() % 500));

    // each task waits for waiter to give
    xSemaphoreTake(waiter, portMAX_DELAY);

    // take chopsticks, left then right
    xSemaphoreTake(chopstick[idx1], portMAX_DELAY);
    ESP_LOGI("task", "Philosopher %i took chopstick %i", num, idx1);
    vTaskDelay(pdMS_TO_TICKS(500));
    xSemaphoreTake(chopstick[idx2], portMAX_DELAY); 
    ESP_LOGI("task", "Philosopher %i took chopstick %i", num, idx2);

    // eat
    ESP_LOGI("eat", "Philosopher %i is eating", num);
    vTaskDelay(pdMS_TO_TICKS(500));

    xSemaphoreGive(chopstick[idx2]);
    xSemaphoreGive(chopstick[idx1]);
    xSemaphoreGive(done_eating);
    xSemaphoreGive(waiter);
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

    // configure gpio 07 led on espc3c3 esp-rs board
    // gpio_reset_pin(led_pin);
    // gpio_set_direction(led_pin, GPIO_MODE_OUTPUT);
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI("MAIN", "Starting main app");

    args_sem = xSemaphoreCreateBinary();
    done_eating = xSemaphoreCreateCounting(NUM_TASKS,0);
    for (int i = 0;i < NUM_TASKS;i++) {
        chopstick[i] = xSemaphoreCreateMutex();
    }
    waiter = xSemaphoreCreateMutex();

    // some crude random implementation in addition to random delay % 500ms at beginning of task
    // standard order
    int order[NUM_TASKS];
    for (int i = 0;i<NUM_TASKS;i++) {
        order[i] = i;
    }
    for (int i = NUM_TASKS-1;i>0;i--) {
        int j = esp_random() % (i+1);
        int temp = order[i];
        order[i] = order[j];
        order[j] = temp;
    }

    ESP_LOGI("Order", "%i%i%i%i%i", order[0], order[1], order[2], order[3], order[4]);


    char task_name[20];
    for (int k = 0;k < NUM_TASKS;k++) {
        int i = order[k]; // i = random index
        sprintf(task_name, "Philo_%i", i);
        xTaskCreatePinnedToCore(
            philophers,
            task_name,
            4096,
            (void*)&i,
            1,
            NULL,
            app_cpu
        );
        // in each loop wait until task releases binary sem after &i is passed to task
        xSemaphoreTake(args_sem, portMAX_DELAY);
    }

    // max delay wait for all done_eating are returned
    for (int i =0;i<NUM_TASKS;i++) {
        xSemaphoreTake(done_eating, portMAX_DELAY);
    } 

    ESP_LOGI("MAIN", "All philosophers have eaten");
}
