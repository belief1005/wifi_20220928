
/*
 * Copyright (C) 2015-2018 Alibaba Group Holding Limited
 */

void HAL_Printf(const char *fmt, ...);
int HAL_Snprintf(char *str, const int len, const char *fmt, ...);

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

#ifdef DEPRECATED_LINKKIT
#include "solo.c"
#else
#include "iot_export_linkkit.h"
#include "cJSON.h"

#ifdef AOS_TIMER_SERVICE
#include "iot_export_timer.h"
#endif

#include "livolo_switch.h"

#include "esp_log.h"

#include "bsp.h"

#include "linkkit_solo.h"

static const char* TAG = "linkkit_example_solo";

#define EXAMPLE_TRACE(TAG, ...)                                     \
    do {                                                            \
        HAL_Printf("\033[1;32;40m%s.%d: ", __func__, __LINE__);     \
        HAL_Printf(__VA_ARGS__);                                    \
        HAL_Printf("\033[0m\r\n");                                  \
    } while (0)

#define EXAMPLE_MASTER_DEVID            (0)
#define EXAMPLE_YIELD_TIMEOUT_MS        (200)

typedef struct {
    int master_devid;
    int cloud_connected;
    int master_initialized;
} user_example_ctx_t;

/**
 * These PRODUCT_KEY|PRODUCT_SECRET|DEVICE_NAME|DEVICE_SECRET are listed for demo only
 *
 * When you created your own devices on iot.console.com, you SHOULD replace them with what you got from console
 *
 */

char PRODUCT_KEY[PRODUCT_KEY_MAXLEN] = {0};
char PRODUCT_SECRET[PRODUCT_SECRET_MAXLEN] = {0};
char DEVICE_NAME[DEVICE_NAME_MAXLEN] = {0};
char DEVICE_SECRET[DEVICE_SECRET_MAXLEN] = {0};

static user_example_ctx_t g_user_example_ctx;
void *cloud_out_timer = NULL;

//断网超时重启
static void cloud_out_reset(void *p)
{
    ESP_LOGI(TAG, "------- cloud_out_reset ---------");

    HAL_Reboot();
}


#ifdef AOS_TIMER_SERVICE
    #define NUM_OF_PROPERTYS 1 /* <=30 dont add timer property */
    const char *control_targets_list[NUM_OF_PROPERTYS] = {"SocketPowerState"};

    static int num_of_tsl_type[NUM_OF_TSL_TYPES] = {1, 0, 0}; /* 1:int/enum/bool; 2:float/double; 3:text/date */

    #define NUM_OF_COUNTDOWN_LIST_TARGET 1  /* <=10 */
    const char *countdownlist_target_list[NUM_OF_COUNTDOWN_LIST_TARGET] = {"SocketPowerState"};
    
    #define NUM_OF_LOCAL_TIMER_TARGET 1  /* <=5 */
    const char *localtimer_target_list[NUM_OF_LOCAL_TIMER_TARGET] = {"SocketPowerState"};
#endif

static user_example_ctx_t *user_example_get_ctx(void)
{
    return &g_user_example_ctx;
}

void *example_malloc(size_t size)
{
    return HAL_Malloc(size);
}

void example_free(void *ptr)
{
    HAL_Free(ptr);
}

void user_post_property_json(char *property)
{
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();
    int res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY, (unsigned char *)property, strlen(property));

    ESP_LOGI(TAG, "Property post Response: %s", property);
    return SUCCESS_RETURN;
}

/** Awss Status event callback */
static int user_awss_status_event_handler(int status)
{
    ESP_LOGI(TAG, "Awss Status %d", status);

    return SUCCESS_RETURN;
}

/** cloud connected event callback */
static int user_connected_event_handler(void)
{
    ESP_LOGI(TAG, "Cloud Connected");
    g_user_example_ctx.cloud_connected = 1;

    //结束计时定时器
    if(cloud_out_timer != NULL)
    {
        HAL_Timer_Stop(cloud_out_timer);
        HAL_Timer_Delete(cloud_out_timer);
    }

    //获取开关状态
    get_mcu_switch_sta();

    user_post_dev_info();

    livolo_switch_set_led_wifi_mode(WIFI_LED_MODE_NET_CONNECTED);

    return SUCCESS_RETURN;
}

/** cloud connect fail event callback */
static int user_connect_fail_event_handler(void) 
{
    ESP_LOGI(TAG, "Cloud Connect Fail");

    return SUCCESS_RETURN;
}

/** cloud disconnected event callback */
static int user_disconnected_event_handler(void)
{
    ESP_LOGI(TAG, "Cloud Disconnected");
    g_user_example_ctx.cloud_connected = 0;

    //开启计时定时器
    if (cloud_out_timer == NULL) 
    {
        cloud_out_timer = HAL_Timer_Create("cloud_out_reset", cloud_out_reset, NULL);
    }
    HAL_Timer_Stop(cloud_out_timer);
    HAL_Timer_Start(cloud_out_timer, 10 * 60 * 1000);

    livolo_switch_set_led_wifi_mode(WIFI_LED_MODE_NET_CONNECTING);

    return SUCCESS_RETURN;
}

/** cloud raw_data arrived event callback */
static int user_rawdata_arrived_event_handler(const int devid, const unsigned char *request, const int request_len)
{
    ESP_LOGI(TAG, "Cloud Rawdata Arrived");

    return SUCCESS_RETURN;
}

/* device initialized event callback */
static int user_initialized(const int devid)
{
    ESP_LOGI(TAG, "Device Initialized");
    g_user_example_ctx.master_initialized = 1;

    return SUCCESS_RETURN;
}

/** recv property post response message from cloud **/
static int user_report_reply_event_handler(const int devid, const int msgid, const int code, const char *reply,
        const int reply_len)
{
    ESP_LOGI(TAG, "Message Post Reply Received, Message ID: %d, Code: %d, Reply: %.*s", msgid, code,
                  reply_len,
                  (reply == NULL)? ("NULL") : (reply));
    return SUCCESS_RETURN;
}

/** recv event post response message from cloud **/
static int user_trigger_event_reply_event_handler(const int devid, const int msgid, const int code, const char *eventid,
        const int eventid_len, const char *message, const int message_len)
{
    ESP_LOGI(TAG, "Trigger Event Reply Received, Message ID: %d, Code: %d, EventID: %.*s, Message: %.*s",
                  msgid, code,
                  eventid_len,
                  eventid, message_len, message);

    return SUCCESS_RETURN;
}

static int user_property_set_event_handler(const int devid, const char *request, const int request_len)
{
    int res = 0;
    cJSON *root = NULL, *LightSwitch = NULL, *LightColor = NULL;

    cJSON *item_cmdUp = NULL, *item_cmdDown = NULL, *item_SocketPowerState = NULL, *item_powerstate = NULL, *item_RemainingTime = NULL, *item_LocalTimer = NULL, *item_CountDownList = NULL;

    cJSON *item_PowerSwitch_1 = NULL, *item_PowerSwitch_2 = NULL, *item_PowerSwitch_3 = NULL;

    ESP_LOGI(TAG,"Property Set Received, Devid: %d, Request: %s", devid, request);

    //add by HTQ
    if (!request) {
        return NULL_VALUE_ERROR;
    }

    /* Parse Root */
    root = cJSON_Parse(request);
    if (!root) {
        ESP_LOGI(TAG,"JSON Parse Error");
        return FAIL_RETURN;
    }

    //add by HTQ
    /*  cmdUp  */

    /*  cmdDown  */

    /*  SocketPowerState  */

    /*  powerstate  */

    /*  PowerSwitch_1  */
    item_PowerSwitch_1 = cJSON_GetObjectItem(root, "PowerSwitch_1");
    if(item_PowerSwitch_1)
    {
        ESP_LOGI(TAG, "PowerSwitch_1: %d", item_PowerSwitch_1->valueint);
        livolo_switch_set_powerstate(Switch1, item_PowerSwitch_1->valueint);
    }

    /*  PowerSwitch_2  */
    item_PowerSwitch_2 = cJSON_GetObjectItem(root, "PowerSwitch_2");
    if(item_PowerSwitch_2)
    {
        ESP_LOGI(TAG, "PowerSwitch_2: %d", item_PowerSwitch_2->valueint);
        livolo_switch_set_powerstate(Switch2, item_PowerSwitch_2->valueint);
    }
    
    /*  PowerSwitch_3  */
    item_PowerSwitch_3 = cJSON_GetObjectItem(root, "PowerSwitch_3");
    if(item_PowerSwitch_3)
    {
        ESP_LOGI(TAG, "PowerSwitch_3: %d", item_PowerSwitch_3->valueint);
        livolo_switch_set_powerstate(Switch3, item_PowerSwitch_3->valueint);
    }
    
#ifdef AOS_TIMER_SERVICE
    /*  CountDownList  */
    item_CountDownList = cJSON_GetObjectItem(root, "CountDownList");
    if(item_CountDownList)
    {
        ESP_LOGI(TAG, "timer_service_property_set");
        timer_service_property_set(request);

        res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_POST_PROPERTY,
                             (unsigned char *)request, request_len);
        ESP_LOGI(TAG,"Post Property Message ID: %d", res);
    }
#endif

    cJSON_Delete(root);

    //res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_POST_PROPERTY,
    //                         (unsigned char *)request, request_len);
    ESP_LOGI(TAG,"Post Property Message ID: %d", res);

    return SUCCESS_RETURN;
}

static int user_property_get_event_handler(const int devid, const char *request, const int request_len, char **response,
                                           int *response_len)
{
    cJSON *request_root = NULL, *item_propertyid = NULL;
    cJSON *response_root = NULL;
    int index = 0;
    ESP_LOGI(TAG, "Property Get Received, Devid: %d, Request: %s", devid, request);

    /* Parse Request */
    request_root = cJSON_Parse(request);
    if (request_root == NULL || !cJSON_IsArray(request_root)) {
        ESP_LOGE(TAG, "JSON Parse Error");
        return -1;
    }

    /* Prepare Response */
    response_root = cJSON_CreateObject();
    if (response_root == NULL) {
        ESP_LOGE(TAG, "No Enough Memory");
        cJSON_Delete(request_root);
        return -1;
    }

    for (index = 0; index < cJSON_GetArraySize(request_root); index++) {
        item_propertyid = cJSON_GetArrayItem(request_root, index);
        if (item_propertyid == NULL || !cJSON_IsString(item_propertyid)) {
            ESP_LOGE(TAG, "JSON Parse Error");
            cJSON_Delete(request_root);
            cJSON_Delete(response_root);
            return -1;
        }

        ESP_LOGI(TAG, "Property ID, index: %d, Value: %s", index, item_propertyid->valuestring);

        if (strcmp("WIFI_Tx_Rate", item_propertyid->valuestring) == 0) {
            cJSON_AddNumberToObject(response_root, "WIFI_Tx_Rate", 1111);
        } else if (strcmp("WIFI_Rx_Rate", item_propertyid->valuestring) == 0) {
            cJSON_AddNumberToObject(response_root, "WIFI_Rx_Rate", 2222);
        } else if (strcmp("LocalTimer", item_propertyid->valuestring) == 0) {
            cJSON *array_localtimer = cJSON_CreateArray();
            if (array_localtimer == NULL) {
                cJSON_Delete(request_root);
                cJSON_Delete(response_root);
                return -1;
            }

            cJSON *item_localtimer = cJSON_CreateObject();
            if (item_localtimer == NULL) {
                cJSON_Delete(request_root);
                cJSON_Delete(response_root);
                cJSON_Delete(array_localtimer);
                return -1;
            }
            cJSON_AddStringToObject(item_localtimer, "Timer", "10 11 * * * 1 2 3 4 5");
            cJSON_AddNumberToObject(item_localtimer, "Enable", 1);
            cJSON_AddNumberToObject(item_localtimer, "IsValid", 1);
            cJSON_AddItemToArray(array_localtimer, item_localtimer);
            cJSON_AddItemToObject(response_root, "LocalTimer", array_localtimer);
        
#ifdef AOS_TIMER_SERVICE
        } else if (strcmp("LocalTimer", item_propertyid->valuestring) == 0) {
            char *local_timer_str = NULL;

            if (NULL != (local_timer_str = timer_service_property_get("[\"LocalTimer\"]"))) {
                ESP_LOGI(TAG, "local_timer %s", local_timer_str);
                cJSON *property = NULL, *value = NULL;

                property = cJSON_Parse(local_timer_str);
                if (property == NULL) {
                    ESP_LOGE(TAG, "No Enough Memory");
                    continue;
                }
                value = cJSON_GetObjectItem(property, "LocalTimer");
                if (value == NULL) {
                    ESP_LOGE(TAG, "No Enough Memory");
                    cJSON_Delete(property);
                    continue;
                }
                cJSON *dup_value = cJSON_Duplicate(value, 1);

                cJSON_AddItemToObject(response_root, "LocalTimer", dup_value);
                cJSON_Delete(property);
                example_free(local_timer_str);
            } else {
                cJSON *array = cJSON_CreateArray();
                cJSON_AddItemToObject(response_root, "LocalTimer", array);
            }
        } else if (strcmp("CountDownList", item_propertyid->valuestring) == 0) {
            char *count_down_list_str = NULL;

            if (NULL != (count_down_list_str = timer_service_property_get("[\"CountDownList\"]"))) {
                ESP_LOGI(TAG, "CountDownList %s", count_down_list_str);
                cJSON *property = NULL, *value = NULL;

                property = cJSON_Parse(count_down_list_str);
                if (property == NULL) {
                    ESP_LOGE(TAG, "No Enough Memory");
                    continue;
                }
                value = cJSON_GetObjectItem(property, "CountDownList");
                if (value == NULL) {
                    ESP_LOGE(TAG, "No Enough Memory");
                    cJSON_Delete(property);
                    continue;
                }
                cJSON *dup_value = cJSON_Duplicate(value, 1);

                cJSON_AddItemToObject(response_root, "CountDownList", dup_value);
                cJSON_Delete(property);
                example_free(count_down_list_str);
            } else {
                cJSON_AddStringToObject(response_root, "CountDownList", "");
            }
#endif
        } else if((strcmp("PowerSwitch_1", item_propertyid->valuestring) == 0)){
            cJSON_AddNumberToObject(response_root, "PowerSwitch_1", livolo_switch_get_powerstate(Switch1));
        } else if((strcmp("PowerSwitch_2", item_propertyid->valuestring) == 0)){
            cJSON_AddNumberToObject(response_root, "PowerSwitch_2", livolo_switch_get_powerstate(Switch2));
        } else if((strcmp("PowerSwitch_3", item_propertyid->valuestring) == 0)){
            cJSON_AddNumberToObject(response_root, "PowerSwitch_3", livolo_switch_get_powerstate(Switch3));
        } else if((strcmp("WiFiRSSI", item_propertyid->valuestring) == 0)){
            cJSON_AddNumberToObject(response_root, "WiFiRSSI", bsp_report_wifi_rssi());
        } else if((strcmp("MacAddress", item_propertyid->valuestring) == 0)){
            char mac[HAL_MAC_LEN + 1] = { 0 };
            HAL_Wifi_Get_Mac( mac );
            cJSON_AddStringToObject(response_root, "MacAddress", mac);
        } else if((strcmp("WifiName", item_propertyid->valuestring) == 0)){
            char ssid[HAL_MAX_SSID_LEN];
            char passwd[HAL_MAX_PASSWD_LEN];
            uint8_t bssid[ETH_ALEN];
            HAL_Wifi_Get_Ap_Info(ssid, passwd, bssid);
            cJSON_AddStringToObject(response_root, "WifiName", ssid);
        } else if((strcmp("Version", item_propertyid->valuestring) == 0)){
            cJSON_AddStringToObject(response_root, "Version", CONFIG_LINKKIT_FIRMWARE_VERSION);
        } else if((strcmp("IPAddress", item_propertyid->valuestring) == 0)){
            char ip_str[NETWORK_ADDR_LEN];
            const char ifname;
            HAL_Wifi_Get_IP(ip_str, &ifname);
            cJSON_AddStringToObject(response_root, "IPAddress", ip_str);
        }
    }
    cJSON_Delete(request_root);

    *response = cJSON_PrintUnformatted(response_root);
    if (*response == NULL) {
        ESP_LOGE(TAG, "No Enough Memory");
        cJSON_Delete(response_root);
        return -1;
    }
    cJSON_Delete(response_root);
    *response_len = strlen(*response);

    ESP_LOGI(TAG, "Property Get Response: %s", *response);

    return SUCCESS_RETURN;
}


static int user_service_request_event_handler(const int devid, const char *serviceid, const int serviceid_len,
        const char *request, const int request_len,
        char **response, int *response_len)
{
    int contrastratio = 0, to_cloud = 0;
    cJSON *root = NULL, *item_transparency = NULL, *item_from_cloud = NULL;
    ESP_LOGI(TAG,"Service Request Received, Devid: %d, Service ID: %.*s, Payload: %s", devid, serviceid_len,
                  serviceid,
                  request);

    /* Parse Root */
    root = cJSON_Parse(request);
    if (root == NULL || !cJSON_IsObject(root)) {
        ESP_LOGE(TAG,"JSON Parse Error");
        return -1;
    }

    if (strlen("Custom") == serviceid_len && memcmp("Custom", serviceid, serviceid_len) == 0) {
        /* Parse Item */
        const char *response_fmt = "{\"Contrastratio\":%d}";
        item_transparency = cJSON_GetObjectItem(root, "transparency");
        if (item_transparency == NULL || !cJSON_IsNumber(item_transparency)) {
            cJSON_Delete(root);
            return -1;
        }
        ESP_LOGI(TAG,"transparency: %d", item_transparency->valueint);
        contrastratio = item_transparency->valueint + 1;

        /* Send Service Response To Cloud */
        *response_len = strlen(response_fmt) + 10 + 1;
        *response = malloc(*response_len);
        if (*response == NULL) {
            ESP_LOGE(TAG,"Memory Not Enough");
            return -1;
        }
        memset(*response, 0, *response_len);
        snprintf(*response, *response_len, response_fmt, contrastratio);
        *response_len = strlen(*response);
    } else if (strlen("SyncService") == serviceid_len && memcmp("SyncService", serviceid, serviceid_len) == 0) {
        /* Parse Item */
        const char *response_fmt = "{\"ToCloud\":%d}";
        item_from_cloud = cJSON_GetObjectItem(root, "FromCloud");
        if (item_from_cloud == NULL || !cJSON_IsNumber(item_from_cloud)) {
            cJSON_Delete(root);
            return -1;
        }
        ESP_LOGI(TAG,"FromCloud: %d", item_from_cloud->valueint);
        to_cloud = item_from_cloud->valueint + 1;

        /* Send Service Response To Cloud */
        *response_len = strlen(response_fmt) + 10 + 1;
        *response = malloc(*response_len);
        if (*response == NULL) {
            ESP_LOGE(TAG,"Memory Not Enough");
            return -1;
        }
        memset(*response, 0, *response_len);
        snprintf(*response, *response_len, response_fmt, to_cloud);
        *response_len = strlen(*response);
    }
    cJSON_Delete(root);

    return 0;
}

static int user_timestamp_reply_event_handler(const char *timestamp)
{
    ESP_LOGI(TAG, "Current Timestamp: %s", timestamp);

    return SUCCESS_RETURN;
}

static int user_topolist_reply_handler(const int devid, const int id, const int code, const char *payload, const int payload_len)
{
    ESP_LOGI(TAG, "ITE_TOPOLIST_REPLY");

    return SUCCESS_RETURN;
}

static int user_permit_join_event_handler(const char *product_key, const int time)
{
    ESP_LOGI(TAG, "ITE_PERMIT_JOIN");
    
    return SUCCESS_RETURN;
}

/** fota event handler **/
static int user_fota_event_handler(int type, const char *version)
{
    char buffer[1025 + 1] = {0};
    int buffer_length = 1025; //must set want read len to len + 1

    /* 0 - new firmware exist, query the new firmware */
    if (type == 0) {
        ESP_LOGI(TAG, "New Firmware Version: %s", version);

        if (IOT_Linkkit_Query(EXAMPLE_MASTER_DEVID, ITM_MSG_QUERY_FOTA_DATA, (unsigned char *)buffer, buffer_length) == SUCCESS_RETURN) {
            HAL_Reboot();
        }
    }

    return 0;
}

/* cota event handler */
static int user_cota_event_handler(int type, const char *config_id, int config_size, const char *get_type,
                                   const char *sign, const char *sign_method, const char *url)
{
    char buffer[128] = {0};
    int buffer_length = 128;

    /* type = 0, new config exist, query the new config */
    if (type == 0) {
        ESP_LOGI(TAG, "New Config ID: %s", config_id);
        ESP_LOGI(TAG, "New Config Size: %d", config_size);
        ESP_LOGI(TAG, "New Config Type: %s", get_type);
        ESP_LOGI(TAG, "New Config Sign: %s", sign);
        ESP_LOGI(TAG, "New Config Sign Method: %s", sign_method);
        ESP_LOGI(TAG, "New Config URL: %s", url);

        IOT_Linkkit_Query(EXAMPLE_MASTER_DEVID, ITM_MSG_QUERY_COTA_DATA, (unsigned char *)buffer, buffer_length);
    }

    return 0;
}

static int user_mqtt_connect_succ_event_handler(void)
{
    ESP_LOGI(TAG, "ITE_MQTT_CONNECT_SUCC");
    
    return SUCCESS_RETURN;
}

static int user_event_notify_handler(const int devid, const char *request, const int request_len)
{
    int res = 0;
    ESP_LOGI(TAG, "Event notify Received, Devid: %d, Request: %s", devid, request);

    res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_EVENT_NOTIFY_REPLY,
                             (unsigned char *)request, request_len);
    ESP_LOGI(TAG, "Post Property Message ID: %d", res);

    return 0;
}

#ifdef AOS_TIMER_SERVICE
static void timer_service_cb(const char *report_data, const char *property_name, int i_value,
                             double d_value, const char * s_value, int prop_idx)
{
    if (prop_idx >= NUM_OF_CONTROL_TARGETS){
        ESP_LOGI(TAG, "ERROR: prop_idx=%d is out of limit=%d", prop_idx, NUM_OF_CONTROL_TARGETS);
    }
    if (report_data != NULL)/* post property to cloud */
        user_post_property_json(report_data);

    if (property_name != NULL){    /* set value to device */
        ESP_LOGI(TAG, "timer_service_cb: property_name=%s prop_idx=%d", property_name, prop_idx);
        if (prop_idx < num_of_tsl_type[0] && strcmp(control_targets_list[0], property_name) == 0)
        {
            //add by HTQ
            livolo_switch_set_SocketPowerState(i_value);

            ESP_LOGI(TAG, "timer_service_cb: int_value=%d", i_value);
            /* set int value */
        }
        else if (prop_idx < num_of_tsl_type[0] + num_of_tsl_type[1]){
            ESP_LOGI(TAG, "timer_service_cb: double_value=%f", d_value);
            /* set doube value */
        }
        else {
            if (s_value != NULL)
                ESP_LOGI(TAG, "timer_service_cb: test_value=%s", s_value);
            /* set string value */
        }
    }

    return;
}
#endif

//add by HTQ
static uint64_t user_update_sec(void)
{
    static uint64_t time_start_ms = 0;

    if (time_start_ms == 0) {
        time_start_ms = HAL_UptimeMs();
    }

    return (HAL_UptimeMs() - time_start_ms) / 1000;
}

//add by HTQ
static int user_master_dev_available(void)
{
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();

    if (user_example_ctx->cloud_connected && user_example_ctx->master_initialized) {
        return 1;
    }

    return 0;
}

#define LINKKIT_FAILED_TIMES        5

static int linkkit_thread(void *paras)
{
    uint64_t time_prev_sec = 0, time_now_sec = 0;
    uint64_t time_begin_sec = 0;
    int res = 0;
    iotx_linkkit_dev_meta_info_t master_meta_info;
    int domain_type = 0, dynamic_register = 0, post_reply_need = 0;

#ifdef ATM_ENABLED
    if (IOT_ATM_Init() < 0) {
        ESP_LOGE(TAG, "IOT ATM init failed!\n");
        return -1;
    }
#endif

    memset(&g_user_example_ctx, 0, sizeof(user_example_ctx_t));

    HAL_GetProductKey(PRODUCT_KEY);
    HAL_GetProductSecret(PRODUCT_SECRET);
    HAL_GetDeviceName(DEVICE_NAME);
    HAL_GetDeviceSecret(DEVICE_SECRET);
    memset(&master_meta_info, 0, sizeof(iotx_linkkit_dev_meta_info_t));
    memcpy(master_meta_info.product_key, PRODUCT_KEY, strlen(PRODUCT_KEY));
    memcpy(master_meta_info.product_secret, PRODUCT_SECRET, strlen(PRODUCT_SECRET));
    memcpy(master_meta_info.device_name, DEVICE_NAME, strlen(DEVICE_NAME));
    memcpy(master_meta_info.device_secret, DEVICE_SECRET, strlen(DEVICE_SECRET));

	if((0 == strlen(master_meta_info.product_key)) || (0 == strlen(master_meta_info.device_name))\
	 ||(0 == strlen(master_meta_info.device_secret)) || (0 == strlen(master_meta_info.product_secret)))
	{
		ESP_LOGE(TAG, "No device meta info found...\n");
		return -1;
	}

    /* Register Callback */
    IOT_RegisterCallback(ITE_AWSS_STATUS, user_awss_status_event_handler);
    IOT_RegisterCallback(ITE_CONNECT_SUCC, user_connected_event_handler);
    IOT_RegisterCallback(ITE_CONNECT_FAIL, user_connect_fail_event_handler);
    IOT_RegisterCallback(ITE_DISCONNECTED, user_disconnected_event_handler);
    IOT_RegisterCallback(ITE_RAWDATA_ARRIVED, user_rawdata_arrived_event_handler);
    IOT_RegisterCallback(ITE_SERVICE_REQUEST, user_service_request_event_handler);
    IOT_RegisterCallback(ITE_PROPERTY_SET, user_property_set_event_handler);
    /*Only for local communication service(ALCS)*/  
    IOT_RegisterCallback(ITE_PROPERTY_GET, user_property_get_event_handler);
    IOT_RegisterCallback(ITE_REPORT_REPLY, user_report_reply_event_handler);
    IOT_RegisterCallback(ITE_TRIGGER_EVENT_REPLY, user_trigger_event_reply_event_handler);
    IOT_RegisterCallback(ITE_TIMESTAMP_REPLY, user_timestamp_reply_event_handler);
    IOT_RegisterCallback(ITE_TOPOLIST_REPLY, user_topolist_reply_handler);
    IOT_RegisterCallback(ITE_PERMIT_JOIN, user_permit_join_event_handler);
    IOT_RegisterCallback(ITE_INITIALIZE_COMPLETED, user_initialized);
    IOT_RegisterCallback(ITE_FOTA, user_fota_event_handler);
    IOT_RegisterCallback(ITE_COTA, user_cota_event_handler);
    IOT_RegisterCallback(ITE_MQTT_CONNECT_SUCC, user_mqtt_connect_succ_event_handler);
    IOT_RegisterCallback(ITE_EVENT_NOTIFY, user_event_notify_handler);

    domain_type = IOTX_CLOUD_REGION_SINGAPORE;
    IOT_Ioctl(IOTX_IOCTL_SET_DOMAIN, (void *)&domain_type);

    /* Choose Login Method */
    dynamic_register = 0;
    IOT_Ioctl(IOTX_IOCTL_SET_DYNAMIC_REGISTER, (void *)&dynamic_register);

    /* post reply doesn't need */
    post_reply_need = 1;
    IOT_Ioctl(IOTX_IOCTL_RECV_EVENT_REPLY, (void *)&post_reply_need);

    /* Create Master Device Resources */
    g_user_example_ctx.master_devid = IOT_Linkkit_Open(IOTX_LINKKIT_DEV_TYPE_MASTER, &master_meta_info);
    if (g_user_example_ctx.master_devid < 0) {
        ESP_LOGE(TAG, "IOT_Linkkit_Open Failed\n");
        return -1;
    }

    /* Start Connect Aliyun Server */
    res = IOT_Linkkit_Connect(g_user_example_ctx.master_devid);
    if (res < 0) {
        ESP_LOGE(TAG, "IOT_Linkkit_Connect Failed\n");
        IOT_Linkkit_Close(g_user_example_ctx.master_devid);
        return -1;
    }

#ifdef AOS_TIMER_SERVICE
    static bool ntp_update = false;
    int ret = timer_service_init(control_targets_list, NUM_OF_PROPERTYS,
                                 countdownlist_target_list, NUM_OF_COUNTDOWN_LIST_TARGET,
                                 localtimer_target_list, NUM_OF_LOCAL_TIMER_TARGET,
                                 timer_service_cb, num_of_tsl_type, user_connected_event_handler);
    if (ret == 0)
        ntp_update = true;
#endif

    time_begin_sec = user_update_sec();

    while (1) {
        IOT_Linkkit_Yield(EXAMPLE_YIELD_TIMEOUT_MS);

#if 0
        time_now_sec = user_update_sec();
        if (time_prev_sec == time_now_sec) {
            continue;
        }

        /* init timer service */
        if (time_now_sec % 10 == 0 && user_master_dev_available()) {
#ifdef AOS_TIMER_SERVICE
            if (!ntp_update) {
                int ret = timer_service_init( control_targets_list, NUM_OF_PROPERTYS,
                                    countdownlist_target_list,  NUM_OF_COUNTDOWN_LIST_TARGET,
                                    localtimer_target_list,NUM_OF_LOCAL_TIMER_TARGET,
                                    timer_service_cb, num_of_tsl_type, user_connected_event_handler );
                if (ret == 0)
                    ntp_update = true;
            }
#endif
        }

        time_prev_sec = time_now_sec;
#endif
    }

    IOT_Linkkit_Close(g_user_example_ctx.master_devid);

    IOT_DumpMemoryStats(IOT_LOG_DEBUG);
    IOT_SetLogLevel(IOT_LOG_NONE);

    return 0;
}

void linkkit_main(void *paras)
{
    int ret = 0;
    static int failed_cnt = 0;

    while (1) {
        ret = linkkit_thread(NULL);

        if(ret < 0)
        {
            if(failed_cnt++ >= LINKKIT_FAILED_TIMES)
            {
                ESP_LOGI(TAG, "联网失败重启");
                failed_cnt = 0;
                HAL_Reboot();
            }
        }

        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}


//add by HTQ
void user_post_dev_info(void)
{
    int res = 0;
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();
    char mac[HAL_MAC_LEN + 1] = { 0 };
    char ssid[HAL_MAX_SSID_LEN];
    char passwd[HAL_MAX_PASSWD_LEN];
    uint8_t bssid[ETH_ALEN];
    char ip_str[NETWORK_ADDR_LEN];
    const char ifname;
    char buffer[256];
    int rssi = 0, chan = 0;

    memset(buffer, 0, sizeof(buffer));
    HAL_Wifi_Get_Ap_Info(ssid, passwd, bssid);
    sprintf( buffer, "{\"WifiName\":\"%s\"}", ssid );
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                             (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "WifiName Message ID: %d", res);

    memset(buffer, 0, sizeof(buffer));
    sprintf( buffer, "{\"Version\":\"%s\"}", CONFIG_LINKKIT_FIRMWARE_VERSION );
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "Version Message ID: %d", res);

    memset(buffer, 0, sizeof(buffer));
    HAL_Wifi_Get_Mac( mac );
    sprintf( buffer, "{\"MacAddress\":\"%s\"}", mac );
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "MacAddress Message ID: %d", res);

    memset(buffer, 0, sizeof(buffer));
    HAL_Wifi_Get_IP(ip_str, &ifname);
    sprintf( buffer, "{\"IPAddress\":\"%s\"}", ip_str );
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "IPAddress Message ID: %d %s", res, buffer);

    memset(buffer, 0, sizeof(buffer));
    sprintf( buffer, "{\"WiFiRSSI\":%d}", bsp_report_wifi_rssi());
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "WiFiRSSI Message ID: %d %s", res, buffer);

    //周期上报rssi
    user_post_rssi_period();

#ifdef  CONFIG_LIVOLO_WIFI_SWITCH_1W
    memset(buffer, 0, sizeof(buffer));
    sprintf( buffer, "{\"PowerSwitch_1\":%d}", livolo_switch_get_powerstate(Switch1));
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "PowerSwitch_1 Message ID: %d", res);
#endif

#ifdef  CONFIG_LIVOLO_WIFI_SWITCH_2W
    memset(buffer, 0, sizeof(buffer));
    sprintf( buffer, "{\"PowerSwitch_1\":%d}", livolo_switch_get_powerstate(Switch1));
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "PowerSwitch_1 Message ID: %d", res);

    memset(buffer, 0, sizeof(buffer));
    sprintf( buffer, "{\"PowerSwitch_2\":%d}", livolo_switch_get_powerstate(Switch2));
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "PowerSwitch_2 Message ID: %d", res);
#endif

#ifdef  CONFIG_LIVOLO_WIFI_SWITCH_3W
    memset(buffer, 0, sizeof(buffer));
    sprintf( buffer, "{\"PowerSwitch_1\":%d}", livolo_switch_get_powerstate(Switch1));
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "PowerSwitch_1 Message ID: %d", res);

    memset(buffer, 0, sizeof(buffer));
    sprintf( buffer, "{\"PowerSwitch_2\":%d}", livolo_switch_get_powerstate(Switch2));
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "PowerSwitch_2 Message ID: %d", res);

    memset(buffer, 0, sizeof(buffer));
    sprintf( buffer, "{\"PowerSwitch_3\":%d}", livolo_switch_get_powerstate(Switch3));
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "PowerSwitch_3 Message ID: %d", res);
#endif
}

static void user_post_rssi(void)
{
    int res = 0;
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();
    char buffer[256];

    memset(buffer, 0, sizeof(buffer));
    sprintf( buffer, "{\"WiFiRSSI\":%d}", bsp_report_wifi_rssi());
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "WiFiRSSI Message ID: %d %s", res, buffer);
}


#define WIFI_SIGNAL_CHK_TIMEOUT  (15 * 60 * 1000)

void user_post_rssi_period(void)
{
    static TimerHandle_t wifi_timer = NULL;

    if(wifi_timer == NULL)
    {
        wifi_timer = xTimerCreate("wifi_sign", WIFI_SIGNAL_CHK_TIMEOUT / portTICK_RATE_MS,
                                        true, NULL, user_post_rssi);

        xTimerStart(wifi_timer, portMAX_DELAY);
    }
}

void user_post_AutoSocketSta(char sta)
{
    int res = 0;
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();
    char buffer[256];

    memset(buffer, 0, sizeof(buffer));
    sprintf( buffer, "{\"AutoSocketSta\":%d}", sta);

    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "AutoSocketSta Message ID: %d", res);
}

void user_post_SocketPowerState(bool sta)
{
    int res = 0;
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();
    char buffer[256];

    memset(buffer, 0, sizeof(buffer));
    sprintf( buffer, "{\"SocketPowerState\":%d}", sta);

    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "SocketPowerState Message ID: %d", res);
}

void user_post_powerstate(uint8_t index, bool sta)
{
    int res = 0;
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();
    char buffer[256];

    memset(buffer, 0, sizeof(buffer));
    if(index == Switch1)
    {
        sprintf( buffer, "{\"PowerSwitch_1\":%d}", sta);
    }   
    else if(index == Switch2)
    {
        sprintf( buffer, "{\"PowerSwitch_2\":%d}", sta);
    }
    else if(index == Switch3)
    {
        sprintf( buffer, "{\"PowerSwitch_3\":%d}", sta);
    }
    
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "powerstate Message ID: %d", res);
}

void user_post_cmdUp(bool sta)
{
    int res = 0;
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();
    char buffer[256];

    memset(buffer, 0, sizeof(buffer));
    sprintf( buffer, "{\"cmdUp\":%d}", sta);

    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "cmdUp Message ID: %d", res);

}

void user_post_cmdDown(bool sta)
{
    int res = 0;
    user_example_ctx_t *user_example_ctx = user_example_get_ctx();
    char buffer[256];

    memset(buffer, 0, sizeof(buffer));
    sprintf( buffer, "{\"cmdDown\":%d}", sta);

    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY,
                            (unsigned char *)buffer, strlen(buffer));
    ESP_LOGI(TAG, "cmdDown Message ID: %d", res);

}

#endif
