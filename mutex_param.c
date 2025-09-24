
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

static int global_counter = 0;


static const int led_pin = 21;
static int led_delay = 500;

// semaphore handle
static SemaphoreHandle_t bin_sem = NULL;

// standard blink led task
void blink_led(void* parameters) {
    // defreference the pointer to int that is passed as parameter, should break beucause the setup() gets destroyed with load the xTaskCreate parameters, null pointer
    int num = *((int*)parameters);
    xSemaphoreGive(bin_sem);
    ESP_LOGI("parameters", "Received number: %d", num);

    while (1) {
        ESP_LOGI("TAG", "Toggling LED with delay: %d ms", num);
        gpio_set_level(led_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(num));
        gpio_set_level(led_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(num));
    }
}



void app_main(void) {
    //jtag/usb-serial setup
    usb_serial_jtag_driver_config_t usb_serial_cfg = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 1024
    };
    usb_serial_jtag_driver_install(&usb_serial_cfg);
    esp_vfs_usb_serial_jtag_use_driver();

    // no buffer raw, immediate input/output
    setvbuf(stdin, NULL, _IONBF, 0); 
    setvbuf(stdout, NULL, _IONBF, 0); 

    gpio_reset_pin(led_pin);
    gpio_set_direction(led_pin, GPIO_MODE_OUTPUT);
    
    vTaskDelay(pdMS_TO_TICKS(5000)); 

    // get input from user and send to the task
    
    long int delay_input;
    char buf[32];
    ESP_LOGI("TAG", "Enter delay in milliseconds for LED blinking:");
    fgets(buf, sizeof(buf), stdin);
    delay_input = strtol(buf, NULL, 10);



    // use mutex for variable sharing
    bin_sem = xSemaphoreCreateBinary();
    
    xTaskCreatePinnedToCore(
        blink_led, 
        "blink led",
        4096,
        (void*)&delay_input,
        1,
        NULL,
        app_cpu
    );

    // take semaphore so setup and main don't return and parameter goes out of scope
    xSemaphoreTake(bin_sem, portMAX_DELAY);

    // delete setup() making the param go out of scope.
    // vTaskDelete(NULL);
}
