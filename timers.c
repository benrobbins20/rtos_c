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
 #include "freertos/timers.h"

// #if CONFIG_FREERTOS_UNICORE
// static const BaseType_t app_cpu = 0;
// #else
// static const BaseType_t app_cpu = 1;
// #endif


static TimerHandle_t per_timer = NULL; // one shot

void timerCallback(TimerHandle_t xTimer) {
    ESP_LOGI("TIMER", "Timer expired"); 
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

    per_timer = xTimerCreate(
        "One Shot timer", 
        pdMS_TO_TICKS(2000), 
        pdFALSE,        // one shot, no autoreload
        (void *)0,      // void pointer to ID
        timerCallback   // do callback
    );

    if (per_timer == NULL) {
        ESP_LOGI("TIMER", "Did not create timer");
    }
    else {
        vTaskDelay(pdMS_TO_TICKS(1000));
        xTimerStart(per_timer, portMAX_DELAY);
    }
}
