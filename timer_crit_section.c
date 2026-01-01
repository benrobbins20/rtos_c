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


#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

#define led_pin             7
#define TIMER_DIVIDER       80 // APB 80 MHz, 80/div
#define TICK_COUNT          900000

// 1 MHz, 900_000 ticks, .9 seconds

gptimer_handle_t timer = NULL;
static volatile bool led_state;
static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
static volatile int counter;


bool IRAM_ATTR timer_callback(void *args) {
    // esp macros for freertos 
    portENTER_CRITICAL_ISR(&spinlock);
    counter++;
    portEXIT_CRITICAL_ISR(&spinlock);
    return true; // no yield to higher priority task 'woken'
}


void configure_timer() {
    timer_config_t hw_timer = {
        .divider = TIMER_DIVIDER,
        .auto_reload = TIMER_AUTORELOAD_EN,
        .counter_dir = TIMER_COUNT_UP,
        .intr_type = TIMER_INTR_LEVEL,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN
    };
    timer_init(TIMER_GROUP_0, TIMER_0, &hw_timer);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0); // initial load count
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, TICK_COUNT); // interrupt after tick count
    timer_enable_intr(TIMER_GROUP_0, TIMER_0); // enable interrupt for timer and group
    timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, timer_callback, NULL, 0); // 
    timer_start(TIMER_GROUP_0, TIMER_0);
}



// task to fight the hw timer and decrement the counter
// delay time slightly above the async timer period, get a 'misprint' 
// during the log statement when task not using critical section
void decrement_task(void *pvparameters) {
    while (1) {
        while (counter > 0) {
            ESP_LOGI("task", "%d", counter); // 
            portENTER_CRITICAL(&spinlock);
            counter--;
            portEXIT_CRITICAL(&spinlock);
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 second
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

    // configure gpio 07 led on espc3c3 esp-rs board
    // gpio_reset_pin(led_pin);
    // gpio_set_direction(led_pin, GPIO_MODE_OUTPUT);
    vTaskDelay(pdMS_TO_TICKS(5000));

   

    xTaskCreatePinnedToCore(
        decrement_task,
        "decrement_task",
        2048,
        NULL,
        10,
        NULL,
        app_cpu
    );

    configure_timer(); // start hw timer

    vTaskDelete(NULL); // delete main

}
