#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"

#include "wizchip_conf.h"
#include "wiznet_toe.h"

#if CONFIG_WIZNET_TOE_DISPATCHER_ENABLE

static const char *TAG = "wiznet_toe_disp";
static TaskHandle_t s_dispatcher_task;
static bool s_dispatcher_running;
static wiznet_toe_socket_event_cb_t s_event_cb;
static void *s_event_cb_ctx;

static void wiznet_toe_dispatcher_task(void *arg)
{
    (void)arg;

    while (s_dispatcher_running) {
        uint8_t sir = getSIR();
        uint8_t toe_mask = (uint8_t)(sir & 0xFE); // socket 1..7 only, socket 0 reserved for MACRAW

        if (toe_mask != 0) {
            for (uint8_t sn = 1; sn < 8; sn++) {
                if ((toe_mask & (1U << sn)) == 0) {
                    continue;
                }

                uint8_t sn_ir = getSn_IR(sn);
                if (sn_ir == 0) {
                    continue;
                }

                // W5500 clears socket interrupts by writing back the latched IR bits.
                setSn_IR(sn, sn_ir);

                if (s_event_cb != NULL) {
                    s_event_cb(sn, sn_ir, s_event_cb_ctx);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_WIZNET_TOE_DISPATCHER_PERIOD_MS));
    }

    vTaskDelete(NULL);
}

esp_err_t wiznet_toe_dispatcher_start(void)
{
    if (s_dispatcher_running) {
        return ESP_OK;
    }

    s_dispatcher_running = true;
    BaseType_t rc = xTaskCreate(
        wiznet_toe_dispatcher_task,
        "wiz_toe_disp",
        4096,
        NULL,
        6,
        &s_dispatcher_task
    );

    if (rc != pdPASS) {
        s_dispatcher_running = false;
        s_dispatcher_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "TOE dispatcher started (socket1..7, period=%dms)", CONFIG_WIZNET_TOE_DISPATCHER_PERIOD_MS);
    return ESP_OK;
}

esp_err_t wiznet_toe_dispatcher_stop(void)
{
    if (!s_dispatcher_running) {
        return ESP_OK;
    }

    s_dispatcher_running = false;

    // Let task self-delete on next loop pass.
    for (int i = 0; i < 20; i++) {
        if (eTaskGetState(s_dispatcher_task) == eDeleted) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    s_dispatcher_task = NULL;
    ESP_LOGI(TAG, "TOE dispatcher stopped");
    return ESP_OK;
}

esp_err_t wiznet_toe_register_socket_event_cb(wiznet_toe_socket_event_cb_t cb, void *ctx)
{
    s_event_cb = cb;
    s_event_cb_ctx = ctx;
    return ESP_OK;
}

#else

esp_err_t wiznet_toe_dispatcher_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wiznet_toe_dispatcher_stop(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wiznet_toe_register_socket_event_cb(wiznet_toe_socket_event_cb_t cb, void *ctx)
{
    (void)cb;
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
