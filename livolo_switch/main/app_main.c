/*
 * ESPRSSIF MIT License
 *
 * Copyright (c) 2019 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS ESP32 only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <freertos/timers.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"

#include "iot_export.h"

#include "linkkit_solo.h"
#include "factory_restore.h"

#include "livolo_switch.h"

#include "bsp.h"

#include "conn_mgr.h"

#include "config_param.h"

static const char *TAG = "app main";

static bool linkkit_started = false;
static bool linkkit_stopped = false;

static esp_err_t wifi_event_handle(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_GOT_IP:
            if (linkkit_started == false) {
                wifi_config_t wifi_config = {0};
                if (conn_mgr_get_wifi_config(&wifi_config) == ESP_OK &&
                    strcmp((char *)(wifi_config.sta.ssid), HOTSPOT_AP) &&
                    strcmp((char *)(wifi_config.sta.ssid), ROUTER_AP)) {
                    xTaskCreate((void (*)(void *))linkkit_main, "linkkit_main", 10240, NULL, 5, NULL);
                    linkkit_started = true;
                }
            }
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            linkkit_stopped = false;
            ESP_LOGI(TAG, "sta connected");
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            if (linkkit_stopped == false) {
                linkkit_stopped = true;
                ESP_LOGI(TAG, "sta disconnected");
            }
            break;
        case SYSTEM_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "sta scan done");
            bsp_scan_done_handler();
            break;
        default:
            break;
    }

    return ESP_OK;
}

static void linkkit_event_monitor(int event)
{
    switch (event) {
        case IOTX_AWSS_START: // AWSS start without enbale, just supports device discover
            // operate led to indicate user
            ESP_LOGI(TAG, "IOTX_AWSS_START");
            break;

        case IOTX_AWSS_ENABLE: // AWSS enable, AWSS doesn't parse awss packet until AWSS is enabled.
            ESP_LOGI(TAG, "IOTX_AWSS_ENABLE");
            // operate led to indicate user
            break;

        case IOTX_AWSS_LOCK_CHAN: // AWSS lock channel(Got AWSS sync packet)
            ESP_LOGI(TAG, "IOTX_AWSS_LOCK_CHAN");
            // operate led to indicate user
            break;

        case IOTX_AWSS_PASSWD_ERR: // AWSS decrypt passwd error
            ESP_LOGE(TAG, "IOTX_AWSS_PASSWD_ERR");
            // operate led to indicate user
            break;

        case IOTX_AWSS_GOT_SSID_PASSWD:
            ESP_LOGI(TAG, "IOTX_AWSS_GOT_SSID_PASSWD");
            // operate led to indicate user
            break;

        case IOTX_AWSS_CONNECT_ADHA: // AWSS try to connnect adha (device
            // discover, router solution)
            ESP_LOGI(TAG, "IOTX_AWSS_CONNECT_ADHA");
            // operate led to indicate user
            break;

        case IOTX_AWSS_CONNECT_ADHA_FAIL: // AWSS fails to connect adha
            ESP_LOGE(TAG, "IOTX_AWSS_CONNECT_ADHA_FAIL");
            // operate led to indicate user
            break;

        case IOTX_AWSS_CONNECT_AHA: // AWSS try to connect aha (AP solution)
            ESP_LOGI(TAG, "IOTX_AWSS_CONNECT_AHA");
            // operate led to indicate user
            break;

        case IOTX_AWSS_CONNECT_AHA_FAIL: // AWSS fails to connect aha
            ESP_LOGE(TAG, "IOTX_AWSS_CONNECT_AHA_FAIL");
            // operate led to indicate user
            break;

        case IOTX_AWSS_SETUP_NOTIFY: // AWSS sends out device setup information
            // (AP and router solution)
            ESP_LOGI(TAG, "IOTX_AWSS_SETUP_NOTIFY");
            // operate led to indicate user
            break;

        case IOTX_AWSS_CONNECT_ROUTER: // AWSS try to connect destination router
            ESP_LOGI(TAG, "IOTX_AWSS_CONNECT_ROUTER");
            // operate led to indicate user
            break;

        case IOTX_AWSS_CONNECT_ROUTER_FAIL: // AWSS fails to connect destination
            // router.
            ESP_LOGE(TAG, "IOTX_AWSS_CONNECT_ROUTER_FAIL");
            // operate led to indicate user
            break;

        case IOTX_AWSS_GOT_IP: // AWSS connects destination successfully and got
            // ip address
            ESP_LOGI(TAG, "IOTX_AWSS_GOT_IP");
            // operate led to indicate user
            break;

        case IOTX_AWSS_SUC_NOTIFY: // AWSS sends out success notify (AWSS
            // sucess)
            ESP_LOGI(TAG, "IOTX_AWSS_SUC_NOTIFY");
            // operate led to indicate user
            break;

        case IOTX_AWSS_BIND_NOTIFY: // AWSS sends out bind notify information to
            // support bind between user and device
            ESP_LOGI(TAG, "IOTX_AWSS_BIND_NOTIFY");
            // operate led to indicate user
            break;

        case IOTX_AWSS_ENABLE_TIMEOUT: // AWSS enable timeout
            // user needs to enable awss again to support get ssid & passwd of router
            ESP_LOGW(TAG, "IOTX_AWSS_ENALBE_TIMEOUT");
            // operate led to indicate user

            //配网超时
            livolo_switch_set_wifi_config(false);

            break;

        case IOTX_CONN_CLOUD: // Device try to connect cloud
            ESP_LOGI(TAG, "IOTX_CONN_CLOUD");
            // operate led to indicate user
            break;

        case IOTX_CONN_CLOUD_FAIL: // Device fails to connect cloud, refer to
            // net_sockets.h for error code
            ESP_LOGE(TAG, "IOTX_CONN_CLOUD_FAIL");
            // operate led to indicate user
            break;

        case IOTX_CONN_CLOUD_SUC: // Device connects cloud successfully
            ESP_LOGI(TAG, "IOTX_CONN_CLOUD_SUC");
            // operate led to indicate user
            break;

        case IOTX_RESET: // Linkkit reset success (just got reset response from
            // cloud without any other operation)
            ESP_LOGI(TAG, "IOTX_RESET");
            // operate led to indicate user
            break;

        default:
            break;
    }
}


//3位3开,普通
#define PRODUCT_KEY1        "a1imgCmUtLh"
#define PRODUCT_SECRET1     "5GJ0OX9OVgpfeyvW"
#define DEVICE_NAME1        "wifi_switch_test"
#define DEVICE_SECRET1      "55904fb87b322d6fbf5d238b170b4f8e"

//3位3开,玻璃
#define PRODUCT_KEY2        "a1imgCmUtLh"
#define PRODUCT_SECRET2     "5GJ0OX9OVgpfeyvW"
#define DEVICE_NAME2        "wifi_switch_test3"
#define DEVICE_SECRET2      "c8e6d36e191b5b5a7e5befa639aad9a1"

//2位2开,普通
#define PRODUCT_KEY3        "a1imgCmUtLh"
#define PRODUCT_SECRET3     "5GJ0OX9OVgpfeyvW"
#define DEVICE_NAME3        "wifi_switch_test2"
#define DEVICE_SECRET3      "11d3b9fb4d9a08c2011d05e1eeb8d966"

//2位2开,玻璃
#define PRODUCT_KEY4        "a1imgCmUtLh"
#define PRODUCT_SECRET4     "5GJ0OX9OVgpfeyvW"
#define DEVICE_NAME4        "wifi_switch_test4"
#define DEVICE_SECRET4      "226f0ca92a96bad36aa7a77630dbb5b8"

#ifdef  CONFIG_LIVOLO_WIFI_SWITCH_1W
    #define PRODUCT_KEY_1W          "a1qw374FdPw"
    #define PRODUCT_SECRET_1W       "vyTr58PiHAAqydkG"
#endif

#ifdef  CONFIG_LIVOLO_WIFI_SWITCH_2W
    #define PRODUCT_KEY_2W          "a1DBHZJmarL"
    #define PRODUCT_SECRET_2W       "bPj90ZeG75CknmP7"
#endif

#ifdef  CONFIG_LIVOLO_WIFI_SWITCH_3W
    #define PRODUCT_KEY_3W          "a1MjmtrCvol"
    #define PRODUCT_SECRET_3W       "SJ4QLvCTV2t8CapE"
#endif    


static char product_key[PRODUCT_KEY_LEN + 1] = {0};
static char product_secret[PRODUCT_SECRET_LEN + 1] = {0};
static char device_name[DEVICE_NAME_LEN + 1] = {0};
static char device_secret[DEVICE_SECRET_LEN + 1] = {0};

static int print_app_config(uint8_t type)
{
    int len_kv, ret;
    uint8_t log_level;

    extern esp_err_t HAL_Kv_Init(void);
    HAL_Kv_Init();

    //上电读取日志打印等级
    len_kv = sizeof(uint8_t);
    ret = HAL_Kv_Get(LOG_LEVEL_KV, (void *)&log_level, &len_kv);
    if(ret != ESP_OK)
    {
        printf("log level set failed\r\n");

        log_level = LOG_LEVEL_NONE;
        HAL_Kv_Set(LOG_LEVEL_KV, &log_level, sizeof(uint8_t), 0);
    }
    else
    {
        printf("log level set succeed : %d\r\n", log_level);

        if(log_level == LOG_LEVEL_NONE)
        {
            IOT_SetLogLevel(IOT_LOG_NONE);
        }
        else if(log_level == LOG_LEVEL_INFO)
        {
            IOT_SetLogLevel(IOT_LOG_INFO);
        }
    }

#ifdef  CONFIG_LIVOLO_WIFI_SWITCH_1W
    HAL_SetProductKey(PRODUCT_KEY_1W);
    HAL_SetProductSecret(PRODUCT_SECRET_1W);
#endif

#ifdef  CONFIG_LIVOLO_WIFI_SWITCH_2W
    HAL_SetProductKey(PRODUCT_KEY_2W);
    HAL_SetProductSecret(PRODUCT_SECRET_2W);
#endif

#ifdef  CONFIG_LIVOLO_WIFI_SWITCH_3W
    HAL_SetProductKey(PRODUCT_KEY_3W);
    HAL_SetProductSecret(PRODUCT_SECRET_3W);
#endif

//量产注释
#if 0
    switch(type)
    {
        case 1:
            HAL_SetProductKey(PRODUCT_KEY1);
            HAL_SetProductSecret(PRODUCT_SECRET1);
            
            HAL_SetDeviceName(DEVICE_NAME1);
            HAL_SetDeviceSecret(DEVICE_SECRET1);
            break;

        case 2:
            HAL_SetProductKey(PRODUCT_KEY2);
            HAL_SetProductSecret(PRODUCT_SECRET2);
            
            HAL_SetDeviceName(DEVICE_NAME2);
            HAL_SetDeviceSecret(DEVICE_SECRET2);
            break;

        case 3:
            HAL_SetProductKey(PRODUCT_KEY3);
            HAL_SetProductSecret(PRODUCT_SECRET3);
            
            HAL_SetDeviceName(DEVICE_NAME3);
            HAL_SetDeviceSecret(DEVICE_SECRET3);
            break;

        case 4:
            HAL_SetProductKey(PRODUCT_KEY4);
            HAL_SetProductSecret(PRODUCT_SECRET4);
            
            HAL_SetDeviceName(DEVICE_NAME4);
            HAL_SetDeviceSecret(DEVICE_SECRET4);
            break;

        default:
            break;
    }
#endif

    return 0;
}


static void start_conn_mgr(void)
{
    iotx_event_regist_cb(linkkit_event_monitor);    // awss callback
    conn_mgr_start();

    vTaskDelete(NULL);
}


void app_main(void)
{
    print_app_config(2);

    factory_restore_init();

    conn_mgr_set_cfg_mode(CFG_MODE1);
    conn_mgr_init();
    conn_mgr_register_wifi_event(wifi_event_handle);

    livolo_switch_main();

    xTaskCreate((void (*)(void *))start_conn_mgr, "conn_mgr", 3072, NULL, 5, NULL);
}
