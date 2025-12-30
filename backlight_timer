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

#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

static TimerHandle_t echo_timer = NULL;
#define led_pin 7

void timerCallback(TimerHandle_t xTimer) {
    gpio_set_level(led_pin, 0);
}


void echo(void *parameters) {
    // "buffer" for a serial char
    uint8_t c;

    // read serial input using raw driver and basic serial terminal, vscode uses line buffering
    // minicom -D /dev/cu.usbmodemXXX -b 115200 --noini
    while (1) {
        int n = usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY);
        if (n > 0) {

            // echo back, set 'backlight' high, reset timer
            usb_serial_jtag_write_bytes(&c, 1, portMAX_DELAY);
            gpio_set_level(led_pin, 1);
            xTimerStart(echo_timer, portMAX_DELAY); // set/reset timer
        }
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
    gpio_reset_pin(led_pin);
    gpio_set_direction(led_pin, GPIO_MODE_OUTPUT);
    vTaskDelay(pdMS_TO_TICKS(5000));


    echo_timer= xTimerCreate(
        "Echo",
        pdMS_TO_TICKS(5000),
        pdTRUE, // auto reload
        (void*)1, // ID 1
        timerCallback
    );

    xTaskCreatePinnedToCore(
        echo,
        "echo",
        4096,
        NULL,
        5,
        NULL,
        app_cpu
    );
}
