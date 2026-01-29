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
// adc and print timer                 
#define TIMER_DIVIDER       80 
#define A_TICK_COUNT        100000 // 10Hz
#define B_TICK_COUNT        1000000 // 1Hz
#define adc_pin             1
#define buffer_size         20
#define MSG_BUF_LEN 128


gptimer_handle_t timer = NULL;
static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint16_t adc_val; // 12 bit adc value
static SemaphoreHandle_t adc_sem = NULL;
static SemaphoreHandle_t avg_sem = NULL;
static adc_oneshot_unit_handle_t adc;
static TaskHandle_t adc_read_handle = NULL;


static char msg_buf[MSG_BUF_LEN];
// pointers for the write index 
static volatile uint8_t head = 0;

static volatile uint8_t counter;
// buffer of 12 bit adc 
static volatile uint16_t adc_buffer[buffer_size];
// locking semaphore for buffer

static float adc_avg;

// notifty adc task, can't perform adc reads in ISR unlike arduino
bool IRAM_ATTR timer_callback(void *args) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(adc_read_handle, &woken); // set woken when adc task is unblocked
    return true; // yield
}

// adc task pushes to head pointer
// keep this routine very short in a critical section
void push(uint16_t v) {

    // get next with bounds protection
    uint8_t n = (head + 1) % buffer_size; // largest is 19, (19 + 1) % 20 = 0

    adc_buffer[head] = v;
    head = n;
}

// read 10 times a second with a hardware timer
void adc_read(void* pvParameters) {
    int val;
    // loop and wait for notification from timer ISR
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // wait for timer notif from ISR
        adc_oneshot_read(adc, ADC_CHANNEL_0, &val); // read a uint16 into val

        portENTER_CRITICAL(&spinlock);
        push(val);
        portEXIT_CRITICAL(&spinlock);

        if (counter >= 10) {
            counter = 0;
            xSemaphoreGive(avg_sem);
        } else {
            counter++;
    
        }
        // ESP_LOGI("read adc", "ADC Value: %d\n", val);
    }
}

// 10 samples confirmed in buffer when semaphore is given
// iterate backwards from the head pointer - 1, last sample written
// reverse the direction of push to pull last 10 samples
void calc_avg(void* pvParameters) {
    float sum = 0;
    int idx;
    // wait for semaphore to unlock
    while (1) {
        xSemaphoreTake(avg_sem, portMAX_DELAY);

        // the lowest possible head index is 0
        // (0 - 0 - 1) + 20 % 20 = 19
        // (0 - 1 - 1) + 20 % 20 = 18
        // ...
        // (0 - 8 - 1) + 20 % 20 = 11
        // (0 - 9 - 1) + 20 % 20 = 10
        for (int i=0;i<10;i++) {
            idx = ((head - 1 - i) + buffer_size) % buffer_size;
            sum += adc_buffer[idx];
        }
        portENTER_CRITICAL(&spinlock);
        adc_avg = sum / 10.0;
        portEXIT_CRITICAL(&spinlock);
        sum = 0;
        // ESP_LOGI("ADC AVG", "Average ADC Value: %.2f\n", adc_avg);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}


void printBuf(char* buf, uint8_t len) {
    for (int i = 0; i < len; i++) {
        char buf_char = buf[i];
        ESP_LOGI("TAG", "char: %c = ", buf_char);
        for (int bit = 7; bit >= 0; bit--) {
            printf("%d", (buf_char >> bit) & 1);
        }
        printf("\n");
    }
}

// listen for uart command to print avg
void read_serial(void* parameters) {
    // 
    int c;
    char buf[20] = {0};
    uint8_t idx = 0;
    float avg;

    while (1) {
        c = getchar();  // Read from USB_SERIAL_JTAG


        if (c != EOF) {
            if (c == '\r' || c == '\n') {
                if (idx > 0) {
                    msg_buf[idx] = '\0';

                    if (strcmp(msg_buf, "avg") == 0) {

                        portENTER_CRITICAL(&spinlock);
                        avg = adc_avg;
                        portEXIT_CRITICAL(&spinlock);

                        ESP_LOGI("TAG", "Average ADC Value: %.2f", avg);
                    }
                    else {
                        ESP_LOGI("TAG", "Unknown command: %s", msg_buf);
                    }
                    

                    printBuf(msg_buf, idx);
                    memset(msg_buf, 0, MSG_BUF_LEN);
                    idx = 0;
                }
            } else if (idx < MSG_BUF_LEN - 1) {
                msg_buf[idx++] = (char)c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
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
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, A_TICK_COUNT); // interrupt after tick count
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
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_CHANNEL_0, &chan_cfg));

    // can either use task notify are a semaphore to alert task from ISR
    adc_sem = xSemaphoreCreateBinary();

    // reboot if semaphore fails
    if (adc_sem == NULL) {
        ESP_LOGE("ADC", "Could not create semaphore");
        esp_restart();
    }

    // release this 
    avg_sem = xSemaphoreCreateBinary();

    xTaskCreatePinnedToCore(
        adc_read,
        "adc_read",
        2048,
        NULL,
        1,
        &adc_read_handle,
        app_cpu
    );

    xTaskCreatePinnedToCore(
        calc_avg,
        "calc_avg",
        2048,
        NULL,
        2,
        NULL,
        app_cpu
    );

    xTaskCreatePinnedToCore(
        read_serial,
        "read_serial",
        4096,
        NULL,
        1,
        NULL,
        app_cpu
    );

    configure_timer(); // start hw timer

    vTaskDelete(NULL); // delete main

}
