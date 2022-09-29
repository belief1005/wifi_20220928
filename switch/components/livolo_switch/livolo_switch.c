#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <freertos/timers.h>

#include <string.h>

#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <sys/select.h>

#include "esp_vfs.h"
#include "esp_vfs_dev.h"

#include "driver/uart.h"
#include "driver/hw_timer.h"

#include "iot_export_linkkit.h"
#include "iot_import.h"
#include "iot_export.h"

#include "conn_mgr.h"

#include "esp_log.h"
#include "esp_wifi.h"

#include "livolo_switch.h"

#include "linkkit_solo.h"

#include "factory_restore.h"

#include "bsp.h"

#include "mcli.h"

#include "config_param.h"

#define HEART_BEAT_TIMEOUT  (15 * 1000)
#define SYS_REBOOT_TIMEOUT  (2 * 1000)

/**
 * This example shows how to use the UART driver to handle special UART events.
 *
 * It also reads data from UART0 directly, and echoes it to console.
 *
 * - Port: UART0
 * - Receive (Rx) buffer: on
 * - Transmit (Tx) buffer: on
 * - Flow control: off
 * - Event queue: on
 * - Pin assignment: TxD (default), RxD (default)
 */

#define EX_UART_NUM UART_NUM_0

#define BUF_SIZE (128)
#define RD_BUF_SIZE (BUF_SIZE)
static QueueHandle_t uart0_queue;

static int motor_upload_sta = 0;



static int up_sta = 0;
static int down_sta = 0;

static int power_sta = 0;
static int power_index = 0;

static int socket_sta = 0;

static int switch_sta[SWITCH_CONFIG_NUM] = {0};

static int led_mode = WIFI_LED_MODE_NET_CONNECTING;

//WIFI模块发送部分
static char heart_breat_WIFI[] =     {HEAD1, HEAD2, WIFI_VER, 0x00, 0x00, 0x00, CHK_SUM};   //心跳检测
static char chk_procut_info_WIFI[] = {HEAD1, HEAD2, WIFI_VER, 0x01, 0x00, 0x00, CHK_SUM};   //查询产品信息
static char chk_mcu_mode_WIFI[] =    {HEAD1, HEAD2, WIFI_VER, 0x02, 0x00, 0x00, CHK_SUM};   //查询MCU设定模块工作方式
static char wifi_sta_report_WIFI[] = {HEAD1, HEAD2, WIFI_VER, 0x03, 0x00, 0x01, 0x00, CHK_SUM};   //报告wifi工作状态
static char wifi_reset_WIFI[] =      {HEAD1, HEAD2, WIFI_VER, 0x04, 0x00, 0x00, CHK_SUM};   //重置wifi
static char wifi_mode_reset_WIFI[] = {HEAD1, HEAD2, WIFI_VER, 0x05, 0x00, 0x00, CHK_SUM};   //重置wifi选择模式
static char wifi_config_report[] =   {HEAD1, HEAD2, WIFI_VER, 0x09, 0x00, 0x01, 0x00, CHK_SUM};   //报告wifi是否入网
static char chk_mcu_sta_WIFI[] =     {HEAD1, HEAD2, WIFI_VER, 0x08, 0x00, 0x00, CHK_SUM};   //查询mcu工作状态
static char ota_start_WIFI[] =       {HEAD1, HEAD2, WIFI_VER, 0x0a, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, CHK_SUM};   //升级启动
static char ota_transport_WIFI[] =   {HEAD1, HEAD2, WIFI_VER, 0x0b, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, CHK_SUM};   //升级包传输
static char get_local_time_WIFI[] =  {HEAD1, HEAD2, WIFI_VER, 0x1c, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, CHK_SUM};   //获取本地时间
static char factory_test_WIFI[] =    {HEAD1, HEAD2, WIFI_VER, 0x0e, 0x00, 0x02, 0x00, 0x00, CHK_SUM};   //wifi工程厂测

static char up_action_WIFI[] =       {HEAD1, HEAD2, WIFI_VER, 0x06, 0x00, 0x05, FUNC_CMD_UP_ID, 0x01, 0x00, 0x01, FUNC_CMD_ON_OFF, CHK_SUM};     //上
static char power_action_WIFI[] =    {HEAD1, HEAD2, WIFI_VER, 0x06, 0x00, 0x06, FUNC_CMD_POWER_ID, 0x01, 0x00, 0x02, FUNC_CMD_INDEX, FUNC_CMD_ON_OFF, CHK_SUM};      //控制电源
static char down_action_WIFI[] =     {HEAD1, HEAD2, WIFI_VER, 0x06, 0x00, 0x05, FUNC_CMD_DOWN_ID, 0x01, 0x00, 0x01, FUNC_CMD_ON_OFF, CHK_SUM};       //下
static char socket_power_action_WIFI[] =   {HEAD1, HEAD2, WIFI_VER, 0x06, 0x00, 0x05, FUNC_CMD_SOCKET_ID, 0x01, 0x00, 0x01, FUNC_CMD_ON_OFF, CHK_SUM};     //插座输出供电

static const char *TAG = "livolo_switch";

static uint8_t livolo_switch_data_recv(const char *data, size_t len);
static uint8_t livolo_switch_data_send(uint8_t type, char *data, size_t len);

static int cli_init(void);

static void linkkit_reset(void *p)
{
    //iotx_sdk_reset_local();
    HAL_Reboot();
}

//User can call this function for system reset
void livolo_switch_awss_reset_local(void)
{
    int ret = 0;
    void* reset_local_thread = NULL;
    hal_os_thread_param_t hal_os_thread_param;
    int stack_used = 0;

    memset(&hal_os_thread_param, 0, sizeof(hal_os_thread_param_t));
    hal_os_thread_param.stack_size = 4096;
    hal_os_thread_param.name = "reset";
    ret = HAL_ThreadCreate(&reset_local_thread, (void* (*)(void *))iotx_sdk_reset_local, NULL, &hal_os_thread_param, &stack_used);

    ESP_LOGI(TAG, "================= livolo_switch_awss_reset_local =================");

    //获取芯片可用内存
    ESP_LOGI(TAG, "     esp_get_free_heap_size : %d  \n", esp_get_free_heap_size());
    //获取从未使用过的最小内存
    ESP_LOGI(TAG, "     esp_get_minimum_free_heap_size : %d  \n", esp_get_minimum_free_heap_size());
    
    if(ret != 0)
    {
        ESP_LOGI(TAG, "livolo_switch_awss_reset_local, ThreadCreate failed, %d", ret);
    }
}

void livolo_switch_awss_reset_cloud(void)
{
    int ret = 0;
    void* reset_cloud_thread = NULL;
    hal_os_thread_param_t hal_os_thread_param;
    iotx_vendor_dev_reset_type_t reset_type = IOTX_VENDOR_DEV_RESET_TYPE_UNBIND_ONLY;
    int stack_used = 0;

    memset(&hal_os_thread_param, 0, sizeof(hal_os_thread_param_t));
    hal_os_thread_param.stack_size = 4096;
    hal_os_thread_param.name = "reset";
    ret = HAL_ThreadCreate(&reset_cloud_thread, (void* (*)(void *))iotx_sdk_reset_cloud, (void*)&reset_type, &hal_os_thread_param, &stack_used);

    ESP_LOGI(TAG, "================= livolo_switch_awss_reset_cloud =================");

    //获取芯片可用内存
    ESP_LOGI(TAG, "     esp_get_free_heap_size : %d  \n", esp_get_free_heap_size());
    //获取从未使用过的最小内存
    ESP_LOGI(TAG, "     esp_get_minimum_free_heap_size : %d  \n", esp_get_minimum_free_heap_size());
    
    if(ret != 0)
    {
        ESP_LOGI(TAG, "livolo_switch_awss_reset_cloud, ThreadCreate failed, %d", ret);
    }
}

void livolo_switch_awss_reset_all(void)
{
    int ret = 0;
    void* reset_all_thread = NULL;
    hal_os_thread_param_t hal_os_thread_param;
    iotx_vendor_dev_reset_type_t reset_type = IOTX_VENDOR_DEV_RESET_TYPE_UNBIND_ONLY;
    int stack_used = 0;

    memset(&hal_os_thread_param, 0, sizeof(hal_os_thread_param_t));
    hal_os_thread_param.stack_size = 4096;
    hal_os_thread_param.name = "reset";
    ret = HAL_ThreadCreate(&reset_all_thread, (void* (*)(void *))iotx_sdk_reset, (void*)&reset_type, &hal_os_thread_param, &stack_used);

    ESP_LOGI(TAG, "================= livolo_switch_awss_reset_all =================");

    //获取芯片可用内存
    ESP_LOGI(TAG, "     esp_get_free_heap_size : %d  \n", esp_get_free_heap_size());
    //获取从未使用过的最小内存
    ESP_LOGI(TAG, "     esp_get_minimum_free_heap_size : %d  \n", esp_get_minimum_free_heap_size());
    
    if(ret != 0)
    {
        ESP_LOGI(TAG, "livolo_switch_awss_reset_all, ThreadCreate failed, %d", ret);
    }
}

void livolo_switch_system_reboot(void)
{
    void *reset_timer = NULL;

    reset_timer = HAL_Timer_Create("reset", linkkit_reset, NULL);
    HAL_Timer_Stop(reset_timer);
    HAL_Timer_Start(reset_timer, 3000);
}

//数据校验和
static char livolo_switch_chksum(const char *data, size_t data_len)
{
    uint16_t sum = 0;

    for(uint8_t i = 0; i<data_len; i++)
    {
        //ESP_LOGI(TAG, "=== data:%x ===", data[i]);
        sum += data[i];
    }

    //ESP_LOGI(TAG, "=== sum:%x ===", sum);

    return (char)sum;
}

static char livolo_switch_get_chksum(const char *data, size_t data_len)
{
    char chksum = 0x00;

    uint16_t len = (data[4] << 8 | data[5]);

    chksum = *(data + 5 + len + 1);

    return chksum;
}


//心跳15s发一次
static void livolo_switch_heart_beat(void)
{
    char data_arry[5] = {0};
    size_t data_len = 0;
    static bool sta = 0;

    //ESP_LOGI(TAG, "================= livolo_switch_heart_beat =================");

    livolo_switch_data_send(FUNC_CMD_HEART_BEAT, data_arry, data_len);
}


/******************** WIFI模块接收部分 *********************/

//心跳检测
static void heart_beat_func(const char *data, size_t len)
{
    char heart_cnt = data[0];

    ESP_LOGI(TAG, "================= heart_beat_func =================");

    if(len == 0x0001)
    {   
        if(heart_cnt == 0x00)
        {
            //第一次
        }
        else if(heart_cnt == 0x01)
        {
            //其他
        }
    }
}

//查询产品信息
static void chk_procut_info_func(const char *data, size_t len)
{
    char mode = data[0];

    ESP_LOGI(TAG,  "================= chk_procut_info_func =================");

    if(len == 0x002a)
    {   
        if(mode == 0)
        {   
            //默认配网
        }
        else if(mode == 1)
        {
            //低功耗
        }
        else if(mode == 2)
        {
            //特殊配网
        }
    }
}

//查询MCU设定模块工作方式
static void chk_mcu_mode_func(const char *data, size_t len)
{
    ESP_LOGI(TAG, "================= chk_mcu_mode_func =================");

    if(len == 0x0000)
    {
        //MCU与模块配合
    }
    else if(len == 0x0002)
    {
        //模块自己处理
    }
}

//报告wifi工作状态
static void wifi_sta_report_func(const char *data, size_t len)
{
    ESP_LOGI(TAG, "================= wifi_sta_report_func =================");

    if(len == 0x0000)
    {
        
    }
}

static void system_reboot(void)
{
    ESP_LOGI(TAG, "================= system_reboot =================");

    HAL_Reboot();
}

#define ONLY_RESET_CLOUD        5
#define LOG_LEVEL_RESET_INFO    3
#define LOG_LEVEL_RESET_NONE    4


//重置wifi
static void wifi_reset_func(const char *data, size_t len)
{
    char data_arry[5] = {0};
    size_t data_len = 0;
    uint8_t log_level;

    char reset_mode = data[0];

    ESP_LOGI(TAG, "================= wifi_reset_func:%d =================", reset_mode);

    if(len == 0x0001)
    {
        if(reset_mode == CONN_SC_ZERO_MODE) //smartconfig 配网
        {
            factory_restore_set_conn_sc(CONN_SC_ZERO_MODE);

            //删除所有信息
            livolo_switch_awss_reset_all();

            //延时重启
            livolo_switch_system_reboot();
        }
        else if(reset_mode == CONN_SOFTAP_MODE)  //AP 配网
        {
            factory_restore_set_conn_sc(CONN_SOFTAP_MODE);

            //删除所有信息
            livolo_switch_awss_reset_all();

            //延时重启
            livolo_switch_system_reboot();
        }
        else if(reset_mode == ONLY_RESET_CLOUD)
        {
            livolo_switch_awss_reset_cloud();

            //延时重启
            livolo_switch_system_reboot();
        }
        else if(reset_mode == LOG_LEVEL_RESET_INFO) //WIFI_LOG_LEVEL_INFO
        {
            IOT_SetLogLevel(IOT_LOG_INFO);
            log_level = LOG_LEVEL_INFO;
            HAL_Kv_Set(LOG_LEVEL_KV, &log_level, sizeof(uint8_t), 0);
        }
        else if(reset_mode == LOG_LEVEL_RESET_NONE) //WIFI_LOG_LEVEL_NONE
        {
            IOT_SetLogLevel(IOT_LOG_NONE);
            log_level = LOG_LEVEL_NONE;
            HAL_Kv_Set(LOG_LEVEL_KV, &log_level, sizeof(uint8_t), 0);
        }

        livolo_switch_data_send(FUNC_CMD_WIFI_RESET, data_arry, data_len);
    }
}

//重置wifi选择模式
static void wifi_mode_reset_func(const char *data, size_t len)
{
    char data_arry[5] = {0};
    size_t data_len = 0;

    char wifi_mode = data[0];

    ESP_LOGI(TAG, "================= wifi_mode_reset_func =================");

    if(len == 0x0001)
    {
        //将配网模式写入flash
        //factory_restore_set_conn_sc(wifi_mode);

        livolo_switch_data_send(FUNC_CMD_WIFI_MODE_RESET, data_arry, data_len);
    }
}

//查询 wifi 是否配过网
static void wifi_config_func(const char *data, size_t len)
{
    ESP_LOGI(TAG, "================= wifi_config_func =================");

    if(len == 0x0000)
    {
        
    }
}

//查询mcu工作状态
static void chk_mcu_sta_func(const char *data, size_t len)
{
    char data_arry[5] = {0};
    size_t data_len = 0;

    ESP_LOGI(TAG, "================= chk_mcu_sta_func =================");

    if(len == 0x0003)
    {
        ESP_LOGI(TAG, "================= get len =================");

        switch_sta[0] = data[0];
        switch_sta[1] = data[1];
        switch_sta[2] = data[2];
    }
}

//升级启动
static void ota_start_func(const char *data, size_t len)
{
    char package_size = data[0];

    ESP_LOGI(TAG, "================= ota_start_func =================");

    if(len == 0x0001)
    {
        //升级包分包传输大小，
        //1.0x00 256byte
        //2.0x01 512byte
        //3.0x02 1024 byte
    }
}

//升级包传输
static void ota_transport_func(const char *data, size_t len)
{
    char package_size = 0;

    ESP_LOGI(TAG, "================= ota_transport_func =================");

    if(len == 0x0004 + package_size)
    {
        //前四字节，固定为包偏移，后面为数据内容
    }
}

//获取本地时间
static void get_local_time_func(const char *data, size_t len)
{
    char data_arry[10] = {0};
    size_t data_len = 0;

    ESP_LOGI(TAG, "================= get_local_time_func =================");

    if(len == 0x0000)
    {
    #if 0
        data_len = 8;

        data_arry[0] = get_time_flag;
        data_arry[1] = year;
        data_arry[2] = month;
        data_arry[3] = day;
        data_arry[4] = hour;
        data_arry[5] = min;
        data_arry[6] = sec;
        data_arry[7] = week;
    #else
        data_len = 8;

        time_t now = 0;
        struct tm timeinfo = { 0 };

        time(&now);
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_year < (2019 - 1900)) 
        {
            ESP_LOGI(TAG, "SNTP get time failed, break\n");

            data_arry[0] = 0;   //获取时间失败
            data_arry[1] = 0;
            data_arry[2] = 0;
            data_arry[3] = 0;
            data_arry[4] = 0;
            data_arry[5] = 0;
            data_arry[6] = 0;
            data_arry[7] = 0;
        }
        else
        {
            ESP_LOGI(TAG,"SNTP get time success\n");

            data_arry[0] = 1;   //获取时间成功
            data_arry[1] = (char)timeinfo.tm_year+1900-2000;
            data_arry[2] = (char)timeinfo.tm_mon+1;
            data_arry[3] = (char)timeinfo.tm_mday;
            data_arry[4] = (char)timeinfo.tm_hour;
            data_arry[5] = (char)timeinfo.tm_min;
            data_arry[6] = (char)timeinfo.tm_sec;
            data_arry[7] = (char)timeinfo.tm_wday;
        }
    #endif

        livolo_switch_data_send(FUNC_CMD_GET_LOCAL_TIME, data_arry, data_len);
    }
}


//wifi功能厂测
static void factory_test_func(const char *data, size_t len)
{
    char data_arry[5] = {0};
    size_t data_len = 0;
    char res = 0;
    char code = 0;

    ESP_LOGI(TAG, "================= factory_test_func =================");

    if(len == 0x0000)
    {

    }
}

static void control_func(const char *data, size_t len)
{
    char dpID = data[0];
    size_t data_type = 0;

    int sta = 0;
    int index = 0;

    if(len == 0x0006)
    {
        ESP_LOGI(TAG, "===================== control_func, dpID:%x, sta_data:%x =====================", dpID, data[len-1]);

        switch(dpID)
        {
            case FUNC_CMD_UPLOAD_ID:
                //状态上报
                sta = data[len-1];
                index = data[len-2];

                if(sta == 0x06)
                {
                    switch_sta[index] = OFF;
                    user_post_powerstate(index, switch_sta[index]);
                }
                else if(sta == 0x07)
                {
                    switch_sta[index] = ON;
                    user_post_powerstate(index, switch_sta[index]);
                }
                else
                {
                    user_post_AutoSocketSta(sta);
                }
                
                break;

            case FUNC_CMD_UP_ID:

                //上
                up_sta= data[len-1];
                user_post_cmdUp(up_sta);

                break;

            case FUNC_CMD_POWER_ID:

                //控制电源
                power_sta = data[len-1];
                power_index = data[len-2];
                //user_post_powerstate(power_index, power_sta);

                break;

            case FUNC_CMD_DOWN_ID:

                //下
                down_sta = data[len-1];
                user_post_cmdDown(down_sta);

                break;

            case FUNC_CMD_SOCKET_ID:

                //插座输出供电
                socket_sta = data[len-1];
                user_post_SocketPowerState(socket_sta);

                break; 

            default:
                break;
        }
    }
}


//用于保存粘连数据包中的位置
#define BUFFER_DATA_LEN  15

static char *data_locat_ptr[BUFFER_DATA_LEN] = { NULL };
static uint8_t data_locat_index[BUFFER_DATA_LEN] = { 0 };
static uint8_t data_locat_len[BUFFER_DATA_LEN] = { 0 };
static uint8_t data_locat_cnt = 0;

static void get_how_many_data(const char *data, size_t data_len)
{   
    uint16_t i = 0; 

    for(i=0; i<data_len; i++)
    {
        if(data[i] == HEAD1 && data[i+1] == HEAD2 && data[i+2] == HEAD3)
        {
            //记录数据地址
            data_locat_ptr[data_locat_cnt] = &data[i];

            // 记录正确的数据位置
            data_locat_index[data_locat_cnt] = i;

            data_locat_cnt++;
        }    
    }

    data_locat_index[data_locat_cnt] = data_len;

    for(i=0; i<data_locat_cnt; i++)
    {
        data_locat_len[i] = data_locat_index[i+1] - data_locat_index[i];

        //ESP_LOGI(TAG, "===================== index:%d, data_locat_len:%d, data_locat_index:%d,%d =====================", i, data_locat_len[i], data_locat_index[i+1], data_locat_index[i]);
    }
}


static void clr_buffer_data(void)
{
    uint16_t i = 0; 

    for(i=0; i<BUFFER_DATA_LEN; i++)
    {
        data_locat_ptr[i] =  NULL;
        data_locat_index[i] = 0;
        data_locat_len[i] = 0;
    }

    data_locat_cnt = 0;
}

static uint8_t livolo_switch_data_recv(const char *data, size_t len)
{
    uint8_t ret = 0;
    uint16_t i = 0;

    char tmp_chk_sum = 0;
    const char *real_data_ptr = NULL;
    size_t real_len = 0;
    char real_chksum = 0;

    //MCU端的数据发来，可能粘连在一次，需要对头部做区分
    get_how_many_data(data, len);

    if(data_locat_cnt == 0)
    {
        return 1;
    }

    for(i=0; i<data_locat_cnt; i++)
    {
        real_chksum = 0;
        tmp_chk_sum = 0;
        real_data_ptr = data_locat_ptr[i];
        real_len = data_locat_len[i];
        
        //检查真实波特率
        real_chksum = livolo_switch_get_chksum(real_data_ptr, real_len-1);

        //检查校验码是否正确
        tmp_chk_sum = livolo_switch_chksum(real_data_ptr, real_len-1);

        //ESP_LOGI(TAG, "===================== real_chksum:%x, tmp_chk_sum:%x, cmd:%x =====================", real_chksum, tmp_chk_sum, real_data_ptr[3]);

        if(tmp_chk_sum == real_chksum)
        {
            char cmd_type = real_data_ptr[3];
            uint16_t data_len = (real_data_ptr[4] << 8 | real_data_ptr[5]);
            const char *data_ptr = (real_data_ptr + 6);

            //判断命令字
            switch(cmd_type)
            {
                case CMD_RX_HEART_BEAT:

                    heart_beat_func(data_ptr, data_len);

                    break;

                case CMD_RX_CHK_PRODUCT_INFO:
                    
                    chk_procut_info_func(data_ptr, data_len);

                    break;

                case CMD_RX_CHK_MCU_MODE:
                    
                    chk_mcu_mode_func(data_ptr, data_len);

                    break;

                case CMD_RX_WIFI_STA_REPORT:
                    
                    wifi_sta_report_func(data_ptr, data_len);

                    break;

                case CMD_RX_WIFI_RESET:

                    wifi_reset_func(data_ptr, data_len);

                    break;

                case CMD_RX_WIFI_MODE_RESET:
                    
                    wifi_mode_reset_func(data_ptr, data_len);

                    break;

                case CMD_RX_WIFI_CONFIG:

                    wifi_config_func(data_ptr, data_len);

                    break;

                case CMD_RX_CHK_MCU_STA:
                    
                    chk_mcu_sta_func(data_ptr, data_len);
                    
                    break;

                case CMD_RX_OTA_START:
                    
                    ota_start_func(data_ptr, data_len);

                    break;

                case CMD_RX_OTA_TRANSPORT:
                    
                    ota_transport_func(data_ptr, data_len);
                    
                    break;

                case CMD_RX_GET_LOCAL_TIME:
                    
                    get_local_time_func(data_ptr, data_len);

                    break;

                case CMD_RX_FACTORY_TEST:
                    
                    factory_test_func(data_ptr, data_len);

                    break;

                case CMD_RX_CONTROL:
                    
                    control_func(data_ptr, data_len);

                    break;

                default:
                    break;
            }
        }
        else
        {
            continue;
        }
    }

    clr_buffer_data();

    return 0;
}

static uint8_t livolo_switch_data_send(uint8_t type, char *data, size_t len)
{
    char *tmp_ptr = NULL; 
    size_t tmp_len = 0;

    switch(type)
    {
        case FUNC_CMD_HEART_BEAT:
            tmp_ptr = heart_breat_WIFI;
            tmp_len = sizeof(heart_breat_WIFI)/sizeof(heart_breat_WIFI[0]);
            break;

        case FUNC_CMD_CHK_PRODUCT_INFO:
            tmp_ptr = chk_procut_info_WIFI;
            tmp_len = sizeof(chk_procut_info_WIFI)/sizeof(chk_procut_info_WIFI[0]);
            break;

        case FUNC_CMD_CHK_MCU_MODE:
            tmp_ptr = chk_mcu_mode_WIFI;
            tmp_len = sizeof(chk_mcu_mode_WIFI)/sizeof(chk_mcu_mode_WIFI[0]);
            break;

        case FUNC_CMD_WIFI_STA_REPORT:
            tmp_ptr = wifi_sta_report_WIFI;
            tmp_len = sizeof(wifi_sta_report_WIFI)/sizeof(wifi_sta_report_WIFI[0]);
            break;

        case FUNC_CMD_WIFI_RESET:
            tmp_ptr = wifi_reset_WIFI;
            tmp_len = sizeof(wifi_reset_WIFI)/sizeof(wifi_reset_WIFI[0]);
            break;

        case FUNC_CMD_WIFI_MODE_RESET:
            tmp_ptr = wifi_mode_reset_WIFI;
            tmp_len = sizeof(wifi_mode_reset_WIFI)/sizeof(wifi_mode_reset_WIFI[0]);
            break;

        case FUNC_CMD_WIFI_CONFIG:
            tmp_ptr = wifi_config_report;
            tmp_len = sizeof(wifi_config_report)/sizeof(wifi_config_report[0]);
            break;

        case FUNC_CMD_CHK_MCU_STA:
            tmp_ptr = chk_mcu_sta_WIFI;
            tmp_len = sizeof(chk_mcu_sta_WIFI)/sizeof(chk_mcu_sta_WIFI[0]);
            break;

        case FUNC_CMD_OTA_START:
            tmp_ptr = ota_start_WIFI;
            tmp_len = sizeof(ota_start_WIFI)/sizeof(ota_start_WIFI[0]);
            break;

        case FUNC_CMD_OTA_TRANSPORT:
            tmp_ptr = ota_transport_WIFI;
            tmp_len = sizeof(ota_transport_WIFI)/sizeof(ota_transport_WIFI[0]);
            break;

        case FUNC_CMD_GET_LOCAL_TIME:
            tmp_ptr = get_local_time_WIFI;
            tmp_len = sizeof(get_local_time_WIFI)/sizeof(get_local_time_WIFI[0]);
            break;

        case FUNC_CMD_FACTORY_TEST:
            tmp_ptr = factory_test_WIFI;
            tmp_len = sizeof(factory_test_WIFI)/sizeof(factory_test_WIFI[0]);
            break;

        case FUNC_CMD_UP:
            tmp_ptr = up_action_WIFI;
            tmp_len = sizeof(up_action_WIFI)/sizeof(up_action_WIFI[0]);
            break;

        case FUNC_CMD_DOWN:
            tmp_ptr = down_action_WIFI;
            tmp_len = sizeof(down_action_WIFI)/sizeof(down_action_WIFI[0]);
            break;

        case FUNC_CMD_POWER:
            tmp_ptr = power_action_WIFI;
            tmp_len = sizeof(power_action_WIFI)/sizeof(power_action_WIFI[0]);
            break;

        case FUNC_CMD_SOCKET_POWER:
            tmp_ptr = socket_power_action_WIFI;
            tmp_len = sizeof(socket_power_action_WIFI)/sizeof(socket_power_action_WIFI[0]);
            break;

        default:
            break;
    }

    if(tmp_ptr == NULL)
    {
        return 1;
    }

    if(type > FUNC_CMD_START && type < FUNC_CMD_MID)
    {
        if(len == 0)
        {
            //无
        }
        else if(len == 1)
        {
            //状态变换
            tmp_ptr[tmp_len - 2] = data[0];
        }
        else if(len == 2)
        {
            //厂测功能
            tmp_ptr[tmp_len - 2] = data[1];
            tmp_ptr[tmp_len - 3] = data[0];
        }
        else if(len == 4)
        {
            //软件升级
        }
        else if(len == 8)
        {
            //获取本地时间
            tmp_ptr[tmp_len - 2] = data[7];     //星期
            tmp_ptr[tmp_len - 3] = data[6];     //秒
            tmp_ptr[tmp_len - 4] = data[5];     //分
            tmp_ptr[tmp_len - 5] = data[4];     //时
            tmp_ptr[tmp_len - 6] = data[3];     //日
            tmp_ptr[tmp_len - 7] = data[2];     //月
            tmp_ptr[tmp_len - 8] = data[1];     //年
            tmp_ptr[tmp_len - 9] = data[0];     //是否成功标志
        }
        else
        {
            //其他
            return 1;
        }
    }
    else if(type > FUNC_CMD_MID && type < FUNC_CMD_END)
    {
        if(len == 1)
        {
            tmp_ptr[tmp_len - 2] = data[0];
        }
        else if(len == 2)
        {
            tmp_ptr[tmp_len - 2] = data[1];
            tmp_ptr[tmp_len - 3] = data[0];
        }
        else
        {
            //其他
            return 1;
        }
    }
    else
    {
        return 1;
    }

    //串口发送数据
    tmp_ptr[tmp_len - 1] = livolo_switch_chksum(tmp_ptr, tmp_len-1);      //校验

    printf("\n---------------------------------- send -----------------------------------------\n");
    for(uint8_t i = 0; i<tmp_len; i++)
    {
        printf("%x ",  tmp_ptr[i]);
    }
    printf("\n----------------------------------- end -----------------------------------------\n");

    uart_write_bytes(EX_UART_NUM, (const char *)tmp_ptr, tmp_len);

    vTaskDelay(300 / portTICK_RATE_MS);

    return 0;
}



static void livolo_switch_power_on(void)
{
    char data[5] = {0};
    size_t data_len = 0;
    static TimerHandle_t timer = NULL;

    //检测心跳
    data_len = 0;
    livolo_switch_data_send(FUNC_CMD_HEART_BEAT, data, data_len);

    //查询产品信息
    //data_len = 0;
    //livolo_switch_data_send(FUNC_CMD_CHK_PRODUCT_INFO, data, data_len);

    //查询MCU设定模块工作方式
    //data_len = 0;
    //livolo_switch_data_send(FUNC_CMD_CHK_MCU_MODE, data, data_len);

    //报告wifi工作状态
    //data_len = 1;
    //data[0] = led_mode;
    //livolo_switch_data_send(FUNC_CMD_WIFI_STA_REPORT, data, data_len);

    //查询MCU工作状态
    data_len = 0;
    livolo_switch_data_send(FUNC_CMD_CHK_MCU_STA, data, data_len);

    //发送wifi是否配置
    //data_len = 1;
    //data[0] = led_mode;
    //livolo_switch_data_send(FUNC_CMD_WIFI_CONFIG, data, data_len);

    //15s发送一次心跳
    if(timer == NULL)
    {
        timer = xTimerCreate("heart_beat", HEART_BEAT_TIMEOUT / portTICK_RATE_MS,
                                        true, NULL, livolo_switch_heart_beat);

        xTimerStart(timer, portMAX_DELAY);
    }
}

static void printf_recv_data(const char *data, size_t len)
{
    printf("\n++++++++++++++++++++++++++ recv +++++++++++++++++++++++++++\n");
    for(size_t i=0; i<len; i++)
    {
        printf("%x ", data[i]);
    }
    printf("\n+++++++++++++++++++++++++++ end +++++++++++++++++++++++++++\n");
}


//烧录激活码使用
static bool livolo_switch_burn_secret(const char *data, size_t len)
{
    mcli_parse_buf(data);

    return 0;
}


static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t *dtmp = (uint8_t *) malloc(RD_BUF_SIZE);
    uint8_t ret = 0;

    ESP_LOGI(TAG, "uart_event_task start");

    for (;;) {
        // Waiting for UART event.
        if (xQueueReceive(uart0_queue, (void *)&event, (portTickType)portMAX_DELAY)) {
            bzero(dtmp, RD_BUF_SIZE);

            switch (event.type) {
                // Event of UART receving data
                // We'd better handler data event fast, there would be much more data events than
                // other types of events. If we take too much time on data event, the queue might be full.
                case UART_DATA:
                    //ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);

                    //打印接受到的数据
                    printf_recv_data((const char *) dtmp, event.size);

                    //解析升降插座协议
                    ret = livolo_switch_data_recv((const char *) dtmp, event.size);

                    if(ret == 1)
                    {
                        ESP_LOGI(TAG, "start livolo_switch_burn_secret");

                        //解析激活码
                        livolo_switch_burn_secret((const char *) dtmp, event.size);
                    }

                    //uart_write_bytes(EX_UART_NUM, (const char *) dtmp, event.size);

                    break;

                // Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGI(TAG, "hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;

                // Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGI(TAG, "ring buffer full");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;

                case UART_PARITY_ERR:
                    ESP_LOGI(TAG, "uart parity error");
                    break;

                // Event of UART frame error
                case UART_FRAME_ERR:
                    ESP_LOGI(TAG, "uart frame error");
                    break;

                // Others
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }

    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

void livolo_switch_set_powerstate(uint8_t index, bool sta)
{
    char data_arry[5] = {0};
    size_t data_len = 0;

    ESP_LOGI(TAG, "livolo_switch_set_powerstate, state:%d, switch_sta:%d", sta, switch_sta[index]);
    if (switch_sta[index] == (int)sta) 
    {
        return;
    }

    switch_sta[index] = sta;
    data_arry[0] = index;
    data_arry[1] = sta;
    data_len = 2;

    livolo_switch_data_send(FUNC_CMD_POWER, data_arry, data_len);
}

void livolo_switch_set_SocketPowerState(bool sta)
{
    char data_arry[5] = {0};
    size_t data_len = 0;

    ESP_LOGI(TAG, "livolo_switch_set_SocketPowerState, state:%d, socket_sta:%d", sta, socket_sta);
    if (socket_sta == (int)sta) 
    {
        return;
    }

    socket_sta = sta;
    data_arry[0] = sta;
    data_len = 1;

    livolo_switch_data_send(FUNC_CMD_SOCKET_POWER, data_arry, data_len);
}

void livolo_switch_set_up(bool sta)
{
    char data_arry[5] = {0};
    size_t data_len = 0;

    ESP_LOGI(TAG, "livolo_switch_set_up, state:%d, up_sta:%d", sta, up_sta);
    if (up_sta == (int)sta) 
    {
        return;
    }

    up_sta = sta;
    data_arry[0] = sta;
    data_len = 1;

    livolo_switch_data_send(FUNC_CMD_UP, data_arry, data_len);
}

void livolo_switch_set_down(bool sta)
{
    char data_arry[5] = {0};
    size_t data_len = 0;

    ESP_LOGI(TAG, "livolo_switch_set_down, state:%d, down_sta:%d", sta, down_sta);
    if (down_sta == (int)sta) 
    {
        return;
    }

    down_sta = sta;
    data_arry[0] = sta;
    data_len = 1;

    livolo_switch_data_send(FUNC_CMD_DOWN, data_arry, data_len);
}

void livolo_switch_set_led_wifi_mode(uint8_t mode)
{
    char data_arry[5] = {0};
    size_t data_len = 0;

    data_arry[0] = mode;
    data_len = 1;

    livolo_switch_data_send(FUNC_CMD_WIFI_STA_REPORT, data_arry, data_len);
}

void livolo_switch_set_wifi_config(uint8_t sta)
{
    char data_arry[5] = {0};
    size_t data_len = 0;

    data_arry[0] = sta;
    data_len = 1;

    livolo_switch_data_send(FUNC_CMD_WIFI_CONFIG, data_arry, data_len);
}

void livolo_switch_report_wifi_config(uint8_t config)
{
    char data_arry[5] = {0};
    size_t data_len = 0;

    data_arry[0] = config;
    data_len = 1;

    livolo_switch_data_send(FUNC_CMD_WIFI_CONFIG, data_arry, data_len);
}

//发送厂测是否成功命令
void livolo_switch_report_factory_test(int err_code)
{
    char data_arry[5] = {0};
    size_t data_len = 0;

    data_arry[0] = 0x00; 
    data_arry[1] = err_code;
    data_len = 2;

    livolo_switch_data_send(FUNC_CMD_FACTORY_TEST, data_arry, data_len);
}




void livolo_switch_set_led_mode(uint8_t mode)
{
    led_mode = mode;
}


//esp8266的串口1只有发送功能，串口0可以收发
void livolo_switch_main(void)
{
    // Configure parameters of an UART driver,
    // communication pins and install the driver
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(EX_UART_NUM, &uart_config);

    // Install UART driver, and get the queue.
    uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 100, &uart0_queue, 0);

    //创建mcli模块
    cli_init();

    // Create a task to handler UART event from ISR
    xTaskCreate(uart_event_task, "uart_event_task", 1024 * 3, NULL, 8, NULL);

    bsp_init(); //开启厂测功能

    //上电读取设备状态
    livolo_switch_power_on();

    //恢复保存的插座状态
    //vendor_product_init();
}



//添加mcli模块
static void linkkit_cli_helper(void);
static int log_print_level(int argc, char **argv);
static int meta_data_cfg(int argc, char **argv);

static void cli_write_char(char ch)
{
    uart_write_bytes(EX_UART_NUM, (const char *) &ch, 1);
}

static char cli_read_char(void)
{
    char ch = 0;;
    uint32_t n = 1;

    //uart_read_bytes(EX_UART_NUM, &ch, n, portMAX_DELAY);

    return ch;
}

static mcli_if_t mcli_if = {
    .write_char = cli_write_char,
    .read_char = cli_read_char,
};

int cli_init(void)
{
    mcli_init(&mcli_if);

    linkkit_cli_helper();

    return 0;
}

mcli_cmd_t mdc_cli = {
    .name = "mdc",
    .help = "mdc set",
    .func = meta_data_cfg,
};

mcli_cmd_t log_cli = {
    .name = "log_cli",
    .help = "log_cli set",
    .func = log_print_level,
};

static int strTrim(char *mac_str)
{
    char *from;
    char to[HAL_MAC_LEN + 1];
    int i = 0, j = 0;

    from = mac_str;

    while(i < strlen(from))
    {
        if(from[i] != ':')
        {
            to[j++] = from[i];
        }

        i++;
    }

    to[j] = '\0';

    strncpy(mac_str, to, strlen(to)+1);

    return 0;
}

static int log_print_level(int argc, char **argv)
{
    if(argc == 3)
    {
        if(strstr(argv[1], "LOG_INFO") != NULL)
        {
            IOT_SetLogLevel(IOT_LOG_INFO);
        }
        else if(strstr(argv[1], "LOG_NONE") != NULL)
        {
            IOT_SetLogLevel(IOT_LOG_NONE);
        }
        else
        {

        }

        return 1;
    }

    return 0;
}

static int meta_data_cfg(int argc, char **argv)
{
    char DEVICE_NAME[DEVICE_NAME_MAXLEN] = {0};
    char DEVICE_SECRET[DEVICE_SECRET_MAXLEN] = {0};
    char PRODUCT_KEY[PRODUCT_KEY_MAXLEN] = {0};
    char PRODUCT_SECRET[PRODUCT_SECRET_MAXLEN] = {0};
    char mac[HAL_MAC_LEN + 1] = {0};

    HAL_GetProductKey(PRODUCT_KEY);
    HAL_GetProductSecret(PRODUCT_SECRET);
    HAL_GetDeviceName(DEVICE_NAME);
    HAL_GetDeviceSecret(DEVICE_SECRET);

    if(argc == 4)
    {
        //if(!strcmp(argv[1], "MDC20190612LIVOLO"))
        if(strstr(argv[1], "MDC20190612LIVOLO") != NULL)
        {
            if(strcmp(DEVICE_NAME, argv[2]) != 0 || !strcmp(DEVICE_SECRET, argv[3]) != 0)
            {
                memset(DEVICE_NAME, 0, DEVICE_NAME_MAXLEN);
                memset(DEVICE_SECRET, 0, DEVICE_SECRET_MAXLEN);

                strcpy(DEVICE_NAME, argv[2]);
                strcpy(DEVICE_SECRET, argv[3]);

                //int len = strlen(argv[3]);
                //printf("before len: %d\r\n", len);

                char *p1 = strtok(DEVICE_NAME, "\r");
                printf("name: %s\r\n", DEVICE_NAME);
                mcli_printf("name: %s\r\n", DEVICE_NAME);
                HAL_SetDeviceName(DEVICE_NAME);
                p1 = NULL;
                //for(int i = 0; i<len; i++)
                //{
                //    printf("%x ", DEVICE_SECRET[i]);
                //}
                //printf("\r\n");

                char *p2 = strtok(DEVICE_SECRET, "\r");

                //printf("after %s\r\n", p2);
                //printf("after len: %d\r\n", strlen(p2));

                printf("secret: %s\r\n", DEVICE_SECRET);
                mcli_printf("secret: %s\r\n", DEVICE_SECRET);
                HAL_SetDeviceSecret(DEVICE_SECRET);
                p2 = NULL;
            }

            printf("mdc update new device code\r\n");
            mcli_printf("mdc update new device code\r\n");//烧录软件使用

            vTaskDelay(500 / portTICK_RATE_MS);

            HAL_Reboot();

            return 1;
        }
    }
    else if(argc == 3)
    {
        //printf("argv1:%s\r\n", argv[0]);
        //printf("argv2:%s\r\n", argv[1]);
        //printf("argv3:%s\r\n", argv[2]);

        //if(!strcmp(argv[2], "request"))
        if(strstr(argv[2], "request") != NULL)
        {
            printf("bad operation ###\r\n");
            mcli_printf("bad operation ###\r\n");

            printf("name: %s\r\n", DEVICE_NAME);
            mcli_printf("name: %s\r\n", DEVICE_NAME);

            printf("secret: %s\r\n", DEVICE_SECRET);
            mcli_printf("secret: %s\r\n", DEVICE_SECRET);

            HAL_Wifi_Get_Mac(mac);

            strTrim(mac);

            printf("mdc mac: {%s}\r\n", mac);
            mcli_printf("mdc mac: {%s}\r\n", mac);

#ifdef  CONFIG_LIVOLO_WIFI_SWITCH_1W
            printf("mdc serial: {%s}\r\n", LIVOLO_WIFI_SWITCH_1W);
            mcli_printf("mdc serial: {%s}\r\n", LIVOLO_WIFI_SWITCH_1W);
#endif

#ifdef  CONFIG_LIVOLO_WIFI_SWITCH_2W
            printf("mdc serial: {%s}\r\n", LIVOLO_WIFI_SWITCH_2W);
            mcli_printf("mdc serial: {%s}\r\n", LIVOLO_WIFI_SWITCH_2W);
#endif

#ifdef  CONFIG_LIVOLO_WIFI_SWITCH_3W
            printf("mdc serial: {%s}\r\n", LIVOLO_WIFI_SWITCH_3W);
            mcli_printf("mdc serial: {%s}\r\n", LIVOLO_WIFI_SWITCH_3W);
#endif      

            return 2;
        }
    }
    else
    {
        printf("bad operation ###\r\n");
        mcli_printf("bad operation ###\r\n");

        printf("name: %s\r\n", DEVICE_NAME);
        mcli_printf("name: %s\r\n", DEVICE_NAME);

        printf("secret: %s\r\n", DEVICE_SECRET);
        //mcli_printf("secret: %s\r\n", DEVICE_SECRET);

        return 3;
    }

    return 0;
}

static void linkkit_cli_helper( void )
{
    mcli_cmd_add( &mdc_cli );
    mcli_cmd_add( &log_cli );
}


void get_mcu_switch_sta(void)
{
    char data[5] = {0};
    size_t data_len = 0;

    //查询MCU工作状态
    data_len = 0;
    livolo_switch_data_send(FUNC_CMD_CHK_MCU_STA, data, data_len);
}


//add by HTQ
#if 1
static void product_init_switch(void)
{
    int state = ON;
    int len = sizeof(int);
    int ret = 0;

#if (REBOOT_STATE == LAST_STATE)
    ret = HAL_Kv_Get(KV_KEY_POWER_STATE, (void *)&state, &len);
    ESP_LOGI(TAG, "get kv save switch state:%d, ret:%d", state, ret);
    if (ret != 0)
        state = ON;
#elif (REBOOT_STATE == POWER_OFF)
    state == OFF;
#else
    state == ON;
#endif

    if (state == OFF) 
    {
        livolo_switch_set_SocketPowerState(OFF);
    } 
    else 
    {
        livolo_switch_set_SocketPowerState(ON);
    }
}

#if (REBOOT_STATE == LAST_STATE)

static TimerHandle_t g_timer_period_save_device_status = NULL;

#define PERIOD_SAVE_DEVICE_STATUS_INTERVAL (1000 * 30)

static void timer_period_save_device_status_cb(void *arg1, void *arg2)
{
    int state, len = sizeof(int);
    int ret;

    ret = HAL_Kv_Get(KV_KEY_POWER_STATE, (void *)&state, &len);

    if(ret != 0 || state != socket_sta) {
        ESP_LOGI(TAG, "KV DEVICE_STATUS Update!!!,ret=%d.\n",ret);
        ret = HAL_Kv_Set(KV_KEY_POWER_STATE, &socket_sta, len, 0);
        if (ret < 0)
            ESP_LOGI(TAG, "KV set Error: %d\r\n", __LINE__);
    }
}

#endif

void vendor_product_init(void)
{
    //product_init_switch();

#if (REBOOT_STATE == LAST_STATE)
    if(g_timer_period_save_device_status == NULL)
    {
        g_timer_period_save_device_status = xTimerCreate("save_device_status", PERIOD_SAVE_DEVICE_STATUS_INTERVAL / portTICK_RATE_MS,
                                            true, NULL, timer_period_save_device_status_cb);

        xTimerStart(g_timer_period_save_device_status, portMAX_DELAY);
    }
#endif
}

char livolo_switch_get_sta(void)
{
    return motor_upload_sta;
}

bool livolo_switch_get_powerstate(uint8_t index)
{
    return (bool)switch_sta[index];
}

bool livolo_switch_get_SocketPowerState(void)
{
    return (bool)socket_sta;
}

void livolo_switch_get_up(void)
{
    return (bool)up_sta;
}   

void livolo_switch_get_down(void)
{
    return (bool)down_sta;
}

#endif


//定时上报开关信号

/////////////////////////////////////////////////////////////////////////////

