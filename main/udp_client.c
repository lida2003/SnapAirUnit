/// @file udp_client.c

/*
 * idf header files
 */
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

/*
 * basic header files
 */
#include "config.h"
#include "define.h"

/*
 * module header files
 */
#include "module.h"
#include "mode.h"

/*
 * service header files
 */
#include "udp_client.h"

extern char g_addr_str[STR_IP_LEN];

static void udp_client_task(void *pvParameters)
{
    char rx_buffer[STR_BUFFER_LEN];
    int addr_family = 0;
    int ip_protocol = 0;
    size_t len = 0;

    while (1) {
        len = strlen(g_addr_str);
        if(SW_STATE_INVALID == snap_sw_state_get() 
            || SW_MODE_BT_SPP == snap_sw_mode_get()
            || 0 == len){
#if (DEBUG_UDP_CLT)
            ESP_LOGI(MODULE_UDP_CLT, "invalid switching time: %d-%d-%d", snap_sw_state_get(),snap_sw_mode_get(),len);
#endif /* DEBUG_UDP_CLT */
            vTaskDelay(TIME_ONE_SECOND_IN_MS / portTICK_PERIOD_MS);
            continue;
        }

#if defined(CONFIG_EXAMPLE_IPV4)
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(g_addr_str);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(STATUS_PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
#elif defined(CONFIG_EXAMPLE_IPV6)
        struct sockaddr_in6 dest_addr = { 0 };
        inet6_aton(g_addr_str, &dest_addr.sin6_addr);
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(STATUS_PORT);
        dest_addr.sin6_scope_id = esp_netif_get_netif_impl_index(EXAMPLE_INTERFACE);
        addr_family = AF_INET6;
        ip_protocol = IPPROTO_IPV6;
#endif

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(MODULE_UDP_CLT, "Unable to create socket: errno %d", errno);
            break;
        }

        // Set timeout
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

#if (DEBUG_UDP_CLT)
        ESP_LOGI(MODULE_UDP_CLT, "Socket created, sending to %s:%d", g_addr_str, STATUS_PORT);
#endif /* DEBUG_UDP_CLT */
        while (1) {
            sprintf(rx_buffer, "udp sate =%d mode =%d", snap_sw_state_get(), snap_sw_mode_get());

            int err = sendto(sock, rx_buffer, strlen(rx_buffer), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (err < 0) {
#if (DEBUG_UDP_CLT)
                ESP_LOGE(MODULE_UDP_CLT, "Error occurred during sending: errno %d", errno);
#endif /* DEBUG_UDP_CLT */
                snap_sw_state_degrade(SW_STATE_HALF_DUPLEX);
                break;
            }
#if (DEBUG_UDP_CLT)
            ESP_LOGI(MODULE_UDP_CLT, "Message sent");
#endif /* DEBUG_UDP_CLT */

            struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            // Error occurred during receiving
            if (len < 0) {
#if (DEBUG_UDP_CLT)
                ESP_LOGE(MODULE_UDP_CLT, "recvfrom failed: errno %d", errno);
#endif /* DEBUG_UDP_CLT */
                break;
            }
            // Data received
            else {
                rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string

#if (DEBUG_UDP_CLT)
                ESP_LOGI(MODULE_UDP_CLT, "Received %d bytes from %s:", len, g_addr_str);
                ESP_LOGI(MODULE_UDP_CLT, "%s", rx_buffer);
#endif /* DEBUG_UDP_CLT */

                if (strncmp(rx_buffer, "OK: ", 4) == 0) {
#if (DEBUG_UDP_CLT)
                    ESP_LOGI(MODULE_UDP_CLT, "Received expected message, reconnecting");
#endif /* DEBUG_UDP_CLT */
                    break;
                }
            }

            vTaskDelay(TIME_TWO_SECOND_IN_MS / portTICK_PERIOD_MS);
        }

        if (sock != -1) {
#if (DEBUG_UDP_CLT)
            ESP_LOGE(MODULE_UDP_CLT, "Shutting down socket and restarting...");
#endif /* DEBUG_UDP_CLT */
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

esp_err_t start_udp_client(void)
{
    //ESP_ERROR_CHECK(nvs_flash_init());
    //ESP_ERROR_CHECK(esp_netif_init());
    //ESP_ERROR_CHECK(esp_event_loop_create_default());

    xTaskCreate(udp_client_task, MODULE_UDP_CLT, TASK_LARGE_BUFFER, NULL, 5, NULL);

    return ESP_OK;
}
