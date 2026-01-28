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



#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

#define led_pin             7
#define TIMER_DIVIDER       80 // APB 80 MHz, 80/div
#define TICK_COUNT          1000000 // 1 million ticks = 1 second
#define adc_pin             1

gptimer_handle_t timer = NULL;
static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
static volatile int counter;
static volatile uint16_t adc_val;
static SemaphoreHandle_t adc_sem = NULL;
static adc_oneshot_unit_handle_t adc;
static TaskHandle_t adc_read_handle = NULL;


bool IRAM_ATTR timer_callback(void *args) {
    BaseType_t woken = pdFALSE;

    // notify adc_read
    vTaskNotifyGiveFromISR(adc_read_handle, &woken);

    // or give semaphore to adc_read
    xSemaphoreGiveFromISR(adc_sem, &woken);

    // either yield(true) or return true to yield to other tasks
    // portYIELD_FROM_ISR(woken);
    return true; // true means yield after ISR
}

void adc_read(void* pvParameters) {
    int val;

    // loop and wait for notification from timer ISR
    // while (1) {
    //     ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // wait for timer notif from ISR
    //     adc_oneshot_read(adc, ADC_CHANNEL_1, &val);
    //     ESP_LOGI("read adc", "ADC Value: %d\n", val);
    // }

    // loop and wait for semaphore give from timer ISR
    while (1) {
        if (xSemaphoreTake(adc_sem, portMAX_DELAY) == pdTRUE) {
            adc_oneshot_read(adc, ADC_CHANNEL_1, &val);
            ESP_LOGI("read adc", "ADC Value: %d\n", val);
        }
    }
}


// set up timer to wait 80/80 MHz and 1000000 ticks (1 second timer)
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
    timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, timer_callback, NULL, 0); // timer CB must be bool, true means yield
    timer_start(TIMER_GROUP_0, TIMER_0);
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

    // adc set up
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        // .ulp_mode = default
        // .clk_src = default
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_CHANNEL_1, &chan_cfg));

    // can either use task notify are a semaphore to alert task from ISR
    adc_sem = xSemaphoreCreateBinary();

    // reboot if semaphore fails
    if (adc_sem == NULL) {
        ESP_LOGE("ADC", "Could not create semaphore");
        esp_restart();
    }

    xTaskCreatePinnedToCore(
        adc_read,
        "adc_read",
        2048,
        NULL,
        10,
        &adc_read_handle,
        app_cpu
    );

    configure_timer(); // start hw timer

    vTaskDelete(NULL); // delete main

}
