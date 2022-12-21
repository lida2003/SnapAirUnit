/// @file ttl.c

/*
 * idf header files
 */
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_spp_api.h"
#include "esp_bt_device.h"
#include "esp_bt_defs.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"

/*
 * basic header files
 */
#include "config.h"
#include "define.h"
#include "handle.h"

/*
 * module header files
 */
#include "module.h"
#include "mode.h"
#include "msp_protocol.h"
#include "tello_protocol.h"

/*
 * service header files
 */
#include "ttl.h"
#include "udp_server.h"


static QueueHandle_t g_msp_uart_queue = NULL;

esp_err_t ttl_send(uint8_t * buf, int len)
{
    if(NULL == buf){
        return ESP_FAIL;
    }
    uart_write_bytes(MSP_UART_PORT, buf, len);
    return ESP_OK;
}


static void task_start_ttl(void *pvParameters)
{
    uart_event_t event;

    for (;;) {
        //Waiting for UART event.
        if (xQueueReceive(g_msp_uart_queue, (void * )&event, (TickType_t)portMAX_DELAY)) {
            switch (event.type) {
            //Event of UART receving data
            case UART_DATA:
                if (event.size) {
                    uint8_t * temp = NULL;

                    temp = (uint8_t *)malloc(sizeof(uint8_t)*event.size);
                    if(temp == NULL){
                        ESP_LOGE(MODULE_UART, "%s malloc.1 failed\n", __func__);
                        break;
                    }
                    memset(temp,0x0,event.size);
                    uart_read_bytes(MSP_UART_PORT, temp, event.size, portMAX_DELAY);

#if (DEBUG_UART)
                    ESP_LOGI(MODULE_UART, "ttl read %d Bytes\n", event.size);
                    if (event.size < STR_BUFFER_LEN) {
                        esp_log_buffer_hex(MODULE_UART, temp, event.size);
                    }
#endif /* DEBUG_UART */

                    // To do send to BT SPP
                    if (g_esp_ssp_handle && snap_sw_state_active(SW_MODE_BT_SPP)){
                        if(MESSAGE_CENTER == mspGetMessage()){
                            center_handle_msp_protocol(temp, event.size);
                        } else {
                            ttl_handle_bt_msp_protocol(temp, event.size);
                        }
                        mspSetMessage(MESSAGE_UNKNOW);
                    } else {
                        esp_err_t ret;
                        if (SW_STATE_CLI == snap_sw_state_get()){
                            udp_send_msg(temp, event.size);
                        }else{
                            if(MESSAGE_CENTER == mspGetMessage()){
                                center_handle_msp_protocol(temp, event.size);
                            } else {
                                ret = ttl_handle_wifi_msp_protocol(temp, event.size);
                                if(ESP_OK != ret){
                                    (void) ttl_handle_wifi_nomsp_protocol(temp, event.size);
                                }
                            }
                            mspSetMessage(MESSAGE_UNKNOW);
                        }
                    }
                    //esp_ble_gatts_send_indicate(spp_gatts_if, spp_conn_id, spp_handle_table[SPP_IDX_SPP_DATA_NTY_VAL],event.size, temp, false);
                    free(temp);
                }
                break;

            default:
                break;
            }
        }
    }
    vTaskDelete(NULL);
}

esp_err_t module_ttl_start(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 127,
        .source_clk = UART_SCLK_DEFAULT,
    };

    //Install UART driver, and get the queue.
    uart_driver_install(MSP_UART_PORT, MSP_RX_BUF_SIZE, MSP_TX_BUF_SIZE, MSP_RX_EVENT_SIZE, &g_msp_uart_queue, 0);
    //Set UART parameters
    uart_param_config(MSP_UART_PORT, &uart_config);
    //Set UART pins
    uart_set_pin(MSP_UART_PORT, MSP_UART_TXD, MSP_UART_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    ESP_ERROR_CHECK(snap_sw_module_start(task_start_ttl, true, TASK_BUFFER_3K0, MODULE_UART));

    return ESP_OK;
}


