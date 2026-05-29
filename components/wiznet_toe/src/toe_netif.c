#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "wizchip_conf.h"
#include "wiznet_toe.h"

static const char *TAG = "wiznet_toe_netif";

static esp_err_t toe_netif_transmit(void *h, void *buffer, size_t len)
{
    (void)h;
    (void)buffer;
    (void)len;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_netif_t *wiznet_toe_netif_create(const uint8_t mac[6])
{
    esp_netif_inherent_config_t base_cfg = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base_cfg.if_key = "TOE_ETH";
    base_cfg.if_desc = "toe0";
    base_cfg.route_prio = 5;

    esp_netif_driver_ifconfig_t driver_cfg = {
        .handle = NULL,
        .transmit = toe_netif_transmit,
        .driver_free_rx_buffer = NULL,
    };

    esp_netif_config_t netif_cfg = {
        .base = &base_cfg,
        .driver = &driver_cfg,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };

    esp_netif_t *toe_netif = esp_netif_new(&netif_cfg);
    if (toe_netif == NULL) {
        ESP_LOGE(TAG, "failed to create toe0 netif");
        return NULL;
    }

    if (mac != NULL) {
        uint8_t mac_local[6];
        memcpy(mac_local, mac, sizeof(mac_local));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_mac(toe_netif, mac_local));
    }

    // toe0 uses static IP synced from eth0 DHCP result.
    esp_err_t dhcp_ret = esp_netif_dhcpc_stop(toe_netif);
    if (dhcp_ret != ESP_OK && dhcp_ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "failed to stop DHCP client on toe0: %s", esp_err_to_name(dhcp_ret));
    }

    ESP_LOGI(TAG, "toe0 netif created");
    return toe_netif;
}

esp_err_t wiznet_toe_netif_sync_ip(esp_netif_t *toe_netif, const esp_netif_ip_info_t *ip_info)
{
    ESP_RETURN_ON_FALSE(toe_netif != NULL, ESP_ERR_INVALID_ARG, TAG, "toe_netif is NULL");
    ESP_RETURN_ON_FALSE(ip_info != NULL, ESP_ERR_INVALID_ARG, TAG, "ip_info is NULL");

    return esp_netif_set_ip_info(toe_netif, ip_info);
}

esp_err_t wiznet_toe_apply_ip_to_chip(const esp_netif_ip_info_t *ip_info)
{
    ESP_RETURN_ON_FALSE(ip_info != NULL, ESP_ERR_INVALID_ARG, TAG, "ip_info is NULL");

    wiz_NetInfo net_info;
    ESP_RETURN_ON_ERROR(wiznet_toe_get_netinfo(&net_info), TAG, "failed to read netinfo");

    net_info.ip[0] = esp_ip4_addr1_16(&ip_info->ip);
    net_info.ip[1] = esp_ip4_addr2_16(&ip_info->ip);
    net_info.ip[2] = esp_ip4_addr3_16(&ip_info->ip);
    net_info.ip[3] = esp_ip4_addr4_16(&ip_info->ip);

    net_info.sn[0] = esp_ip4_addr1_16(&ip_info->netmask);
    net_info.sn[1] = esp_ip4_addr2_16(&ip_info->netmask);
    net_info.sn[2] = esp_ip4_addr3_16(&ip_info->netmask);
    net_info.sn[3] = esp_ip4_addr4_16(&ip_info->netmask);

    net_info.gw[0] = esp_ip4_addr1_16(&ip_info->gw);
    net_info.gw[1] = esp_ip4_addr2_16(&ip_info->gw);
    net_info.gw[2] = esp_ip4_addr3_16(&ip_info->gw);
    net_info.gw[3] = esp_ip4_addr4_16(&ip_info->gw);

    net_info.dhcp = NETINFO_STATIC;

    return wiznet_toe_set_netinfo(&net_info);
}
