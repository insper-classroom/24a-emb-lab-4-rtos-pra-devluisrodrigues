/*
 * LED blink with FreeRTOS
 */
#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <task.h>

#include "gfx.h"
#include "ssd1306.h"

#include "pico/stdlib.h"
#include <stdio.h>

const uint BTN_1_OLED = 28;
const uint BTN_2_OLED = 26;
const uint BTN_3_OLED = 27;

const uint LED_1_OLED = 20;
const uint LED_2_OLED = 21;
const uint LED_3_OLED = 22;

const int TRIGGER_PIN = 12;
const int ECHO_PIN = 13;

typedef struct {
    uint32_t event;
    int time;
} EventTime;

QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;

SemaphoreHandle_t xSemaphoreTrigger;

void oled1_btn_led_init(void) {
    gpio_init(LED_1_OLED);
    gpio_set_dir(LED_1_OLED, GPIO_OUT);

    gpio_init(LED_2_OLED);
    gpio_set_dir(LED_2_OLED, GPIO_OUT);

    gpio_init(LED_3_OLED);
    gpio_set_dir(LED_3_OLED, GPIO_OUT);

    gpio_init(BTN_1_OLED);
    gpio_set_dir(BTN_1_OLED, GPIO_IN);

    gpio_init(BTN_2_OLED);
    gpio_set_dir(BTN_2_OLED, GPIO_IN);

    gpio_init(BTN_3_OLED);
    gpio_set_dir(BTN_3_OLED, GPIO_IN);
}

// pin_callback: Função callback do pino do echo.
void pin_callback(uint gpio, uint32_t events) {
    EventTime et;
    et.event = events;
    et.time = to_us_since_boot(get_absolute_time());
    xQueueSendFromISR(xQueueTime, &et, 0);
}

void echo_task(void *p) {
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &pin_callback);

    EventTime et;
    int rise_time = 0;
    int fall_time;
    int distance;
    while (1) {
        if (xQueueReceive(xQueueTime, &et, pdMS_TO_TICKS(1000))) {
            if (et.event == 0x8) {
                rise_time = et.time;
            } else if (et.event == 0x4) {
                fall_time = et.time;
                distance = (fall_time - rise_time) / 58;
                printf("Distance: %d cm\n", distance);
                xQueueSend(xQueueDistance, &distance, 0);
            }
        }
        else{
            printf("CARACAS QUEBROU\n");
            distance = 99999;
            xQueueSend(xQueueDistance, &distance, 0);
        }
    }
}

// trigger_task: Task responsável por gerar o trigger.
void trigger_task(void *p) {

    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);

    while (1) {
        xSemaphoreGive(xSemaphoreTrigger);
        gpio_put(TRIGGER_PIN, 1);
        sleep_us(10);
        gpio_put(TRIGGER_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void oled_task(void *p) {
    printf("Inicializando Driver\n");
    ssd1306_init();

    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    printf("Inicializando btn and LEDs\n");
    oled1_btn_led_init();

    int distance = 0;
    char buffer[50];
    
    while (1) {
        if (xSemaphoreTake(xSemaphoreTrigger, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (xQueueReceive(xQueueDistance, &distance, pdMS_TO_TICKS(100))) {
                gfx_clear_buffer(&disp);
                if (distance > 300){
                    // Sensor falhou
                    gfx_draw_string(&disp, 0, 0, 1, "Sensor falhou");
                }
                else{
                    printf("Distance OLED: %d cm\n", distance);

                    sprintf(buffer, "Distance: %d cm", distance);
                    gfx_draw_string(&disp, 0, 0, 1, buffer);

                    gfx_draw_line(&disp, 0, 15, distance, 15);
                }
                gfx_show(&disp);
            }
        }
    }
}

int main() {
    stdio_init_all();

    xQueueDistance = xQueueCreate(32, sizeof(int));
    xQueueTime = xQueueCreate(32, sizeof(EventTime));

    xSemaphoreTrigger = xSemaphoreCreateBinary();

    xTaskCreate(trigger_task, "trigger", 2048, NULL, 1, NULL);
    xTaskCreate(echo_task, "echo", 2048, NULL, 1, NULL);
    xTaskCreate(oled_task, "oled", 2048, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}
