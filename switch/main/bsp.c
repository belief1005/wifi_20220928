#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <freertos/timers.h>

#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <sys/select.h>

#include "esp_vfs.h"
#include "esp_vfs_dev.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/hw_timer.h"

#include "iot_export_linkkit.h"
#include "iot_import.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "internal/esp_wifi_internal.h"

#include "livolo_switch.h"
#include "bsp.h"

static const char *TAG = "bsp";

static EventGroupHandle_t bsp_scan_event_group;
const int BSP_SCAN_DONE_BIT = BIT2;

int wifi_rssi_now = 0;

static wifi_scan_config_t bsp_wifi_scan_config = {
    .show_hidden = 0,       //扫描显示热点
    .scan_type = 0,
    .scan_time = {
            .passive = 0,
            .active = {
                .min = 100,
                .max = 200
            }
    }
};

//是否烧录激活码
static bool bsp_is_auth_key_set(void)
{
    int len[4] = {0};
    bool ret;

    char product_key[PRODUCT_KEY_LEN + 1] = {0};
    char product_secret[PRODUCT_SECRET_LEN + 1] = {0};
    char device_name[DEVICE_NAME_LEN + 1] = {0};
    char device_secret[DEVICE_SECRET_LEN + 1] = {0};

    len[0] = HAL_GetProductKey(product_key);
    len[1] = HAL_GetProductSecret(product_secret);
    len[2] = HAL_GetDeviceName(device_name);
    len[3] = HAL_GetDeviceSecret(device_secret);

    if(0 == strlen(product_key) || 0 == strlen(product_secret) || 0 == strlen(device_name) || 0 == strlen(device_secret))
    {
        //没有烧录激活码
        ret = false;
    }
    else
    {
        ESP_LOGI("%s", "....................................................\r\n");
        ESP_LOGI(TAG, "Product Key=%s.\r\n", product_key);
        ESP_LOGI(TAG, "Product Secret=%s.\r\n", product_secret);
        ESP_LOGI(TAG, "Device Name=%s.\r\n", device_name);
        ESP_LOGI(TAG, "Device Secret=%s.\r\n", device_secret);
        ESP_LOGI("%s", "....................................................\r\n");

        ret = true;
    }

    return ret;
}


//wifi信号检测
static int wait_for_scan_done(const char *target_ssid)
{
    // esp_err_t ret = ESP_OK;
    // uint16_t wifi_ap_num = 0;
    // wifi_ap_record_t *ap_info = NULL;
    // wifi_scan_config_t scan_config = {
    //         .show_hidden = 0,
    //         .scan_type = 0,
    //         .scan_time = {
    //              .passive = 0,
    //              .active = {
    //                   .min = 100,
    //                   .max = 200
    //              }
    //         }
    // };

    // ret = esp_wifi_scan_start(&scan_config, true);
    // ret |= esp_wifi_scan_get_ap_num(&wifi_ap_num);
    // if (wifi_ap_num) 
    // {
    //     ap_info = (wifi_ap_record_t *)HAL_Malloc(sizeof(wifi_ap_record_t) * wifi_ap_num);
    //     ret |= esp_wifi_scan_get_ap_records(&wifi_ap_num, ap_info);

    //     if (ret == ESP_OK)
    //     {
    //         for (int i = 0; i < wifi_ap_num; ++i) 
    //         {
    //             ESP_LOGI(TAG, "%26.26s   |   % 4d   ", ap_info[i].ssid, ap_info[i].rssi);

    //             if (strcmp(target_ssid, (const char*)ap_info[i].ssid) == 0) 
    //             {
    //                 ret = ESP_OK;
    //                 ESP_LOGI(TAG, "find test AP, ssid : %s", ap_info[i].ssid);
                    
    //                 break;
    //             }
    //             else
    //             {
    //                 ret = ESP_FAIL;
    //             }
    //         }
    //     }

    //     HAL_Free(ap_info);
    // }

    // esp_wifi_scan_stop();

    // return (ret == ESP_OK) ? SUCCESS_RETURN : FAIL_RETURN;


    uint16_t sta_number = 0;
    uint8_t i;
    wifi_ap_record_t *ap_list_buffer;
    int ret = FAIL_RETURN;

    ESP_ERROR_CHECK(esp_wifi_scan_start(&bsp_wifi_scan_config, true));

    xEventGroupWaitBits(bsp_scan_event_group, BSP_SCAN_DONE_BIT, 0, 1, portMAX_DELAY); //线程在此等待wifi热点扫描结束	
    ESP_LOGI(TAG, "WIFI scan done");
    xEventGroupClearBits(bsp_scan_event_group, BSP_SCAN_DONE_BIT);

    esp_wifi_scan_get_ap_num(&sta_number);
    ESP_LOGI(TAG, "Number of access points found: %d", sta_number);
    if(sta_number == 0)
    {
        ESP_LOGE(TAG, "Nothing AP found");	
        return ret;
    }

    ap_list_buffer = (wifi_ap_record_t *)malloc(sta_number * sizeof(wifi_ap_record_t));
    if (ap_list_buffer == NULL) 
    {
        ESP_LOGE(TAG, "Failed to malloc buffer to print scan results");
        return ret;
    }

    if (esp_wifi_scan_get_ap_records(&sta_number, (wifi_ap_record_t *)ap_list_buffer) == ESP_OK) 
    {
        ESP_LOGI(TAG, "========================================================");
		ESP_LOGI(TAG, "       SSID       |       RSSI       |       AUTH       ");
		ESP_LOGI(TAG, "========================================================");

        for (i = 0; i < sta_number; i++) 
        {
            char *authmode;	
			switch(ap_list_buffer[i].authmode)
			{
				case WIFI_AUTH_OPEN:
					authmode = "WIFI_AUTH_OPEN";
				break;
				case WIFI_AUTH_WEP:
					authmode = "WIFI_AUTH_WEP";
				break;
				case WIFI_AUTH_WPA_PSK:
					authmode = "WIFI_AUTH_WPA_PSK";
				break;
				case WIFI_AUTH_WPA2_PSK:
					authmode = "WIFI_AUTH_WPA2_PSK";
				break;
				case WIFI_AUTH_WPA_WPA2_PSK:
					authmode = "WIFI_AUTH_WPA_WPA2_PSK";
				break;
				default:
					authmode = "Unknown";
				break;
			}

            ESP_LOGI(TAG, "%26.26s   |   % 4d   |   %22.22s", ap_list_buffer[i].ssid, ap_list_buffer[i].rssi, authmode);

            if (strcmp(target_ssid, (const char*)ap_list_buffer[i].ssid) == 0) 
            {
                ret = SUCCESS_RETURN;
                ESP_LOGI(TAG, "find test AP, ssid : %s", ap_list_buffer[i].ssid);
                break;
            }
        }
    }

    free(ap_list_buffer);

    return ret;
}


//出厂测试
static int bsp_is_test_ok(int type)
{
    int err_code = ERR_CODE_NONE;
    int rssi = 0;

    if(type == FACTORY_TEST_BOTH || type == FACTORY_TEST_AUTH_CODE_OK)
    {
        //检查是否烧录激活码
        if(bsp_is_auth_key_set() == false)
        {
            ESP_LOGI(TAG, "--------------------- 未烧写激活码 ---------------------");
            err_code |= ERR_CODE_NO_AUTH_CODE;
        }
    }

    if(type == FACTORY_TEST_BOTH || type == FACTORY_TEST_FACTORY_SSID_OK)
    {
        //检查是否扫描到指定ssid
        if(wait_for_scan_done(CONFIG_FACTORY_TEST_SSID) == FAIL_RETURN)
        {
            ESP_LOGI(TAG, "--------------------- 未找到指定 SSID ---------------------");
            err_code |= ERR_CODE_NO_FACTORY_SSID;
        }
    }

    return err_code;
}

static void factory_test_task(void)
{
    int ret;

    ret = bsp_is_test_ok(FACTORY_TEST_BOTH);

    livolo_switch_report_factory_test(ret); //发送结果到MCU

    ESP_LOGI(TAG, "--------------------- 厂测完成 ---------------------");

    //vTaskDelete(NULL);
}


//上报wifi信号强度
int8_t bsp_report_wifi_rssi(void)
{
    int8_t rssi = 0;

    rssi = esp_wifi_get_ap_rssi();

    ESP_LOGI(TAG, "--------------------- rssi : %d ---------------------", rssi);

    return rssi;
}


//wifi 是否扫描完成
void bsp_scan_done_handler(void)
{
    xEventGroupSetBits(bsp_scan_event_group, BSP_SCAN_DONE_BIT);
}


void bsp_init(void)
{
    bsp_scan_event_group = xEventGroupCreate();

    //创建一个
    //xTaskCreate((void (*)(void *))factory_test_task, "factory_test_task", 3072, NULL, 7, NULL);

    factory_test_task();    
}


