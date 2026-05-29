#include "wiznet_toe.h"

esp_err_t wiznet_toe_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wiznet_toe_get_netinfo(wiz_NetInfo *net_info)
{
    (void)net_info;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wiznet_toe_set_netinfo(const wiz_NetInfo *net_info)
{
    (void)net_info;
    return ESP_ERR_NOT_SUPPORTED;
}

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
