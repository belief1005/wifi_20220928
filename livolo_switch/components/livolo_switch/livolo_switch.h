#ifndef _LIVOLO_SWITCH_H_
#define _LIVOLO_SWITCH_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#define HEAD1                       0x55        //֡ͷ1
#define HEAD2                       0xAA        //֡ͷ2
#define HEAD3                       0x03        //֡ͷ3
#define WIFI_VER                    0x00         
#define MCU_VER                     0x03
#define CHK_SUM                     0xFF

#define FUNC_CMD_UPLOAD_ID          0x67 
#define FUNC_CMD_UP_ID              0x68
#define FUNC_CMD_POWER_ID           0x69
#define FUNC_CMD_DOWN_ID            0x6a
#define FUNC_CMD_SOCKET_ID          0x6b

#define FUNC_CMD_ON_OFF             0x00
#define FUNC_CMD_INDEX              0x00

#define WIFI_LED_MODE_SMARTCONFIG         0x00        //smartconfig����ģʽ
#define WIFI_LED_MODE_AP                  0x01        //AP����ģʽ
#define WIFI_LED_MODE_NET_CONNECTING      0x02        //wifi���óɹ�����δ����
#define WIFI_LED_MODE_NET_CONNECTED       0x04        //������·����
#define WIFI_MODE_NONE                    0xFF

#define PLUG_TIME_TASK_NUM              5    //ÿ���������5�鶨ʱ����
#define COUNT_DOWN_TIMER_MAX            5

//����������޸�����
#define LIFTING_SOCKET                  "LiftingSocket"                 //������������
#define LIVOLO_WIFI_SWITCH_1W           "livolo_wifi_switch_1W"         //wifi����-1·
#define LIVOLO_WIFI_SWITCH_2W           "livolo_wifi_switch_2W"         //wifi����-2·
#define LIVOLO_WIFI_SWITCH_3W           "livolo_wifi_switch_3W"         //wifi����-3·

#define SWITCH_CONFIG_NUM               3

typedef enum {
    OFF = 0,
    ON
} eSwitchState;

typedef enum {
    POWER_OFF = 0,
    POWER_ON = 1,
    LAST_STATE
} eRebootState;

#define KV_KEY_POWER_STATE "kv_key_power_state"

#define REBOOT_STATE LAST_STATE

typedef struct {
	int8_t hour;      //Сʱ
	int8_t minute;    //����
	uint8_t repeat; //bit7:һ��   bit6-0:����-��һ
	int8_t action;    //����
	int8_t on;    //����
} user_plug_task_config_t;


enum{
    FUNC_CMD_START,                 //������ʼ
    FUNC_CMD_HEART_BEAT,
    FUNC_CMD_CHK_PRODUCT_INFO,
    FUNC_CMD_CHK_MCU_MODE,
    FUNC_CMD_WIFI_STA_REPORT,
    FUNC_CMD_WIFI_RESET,
    FUNC_CMD_WIFI_MODE_RESET,
    FUNC_CMD_WIFI_CONFIG,
    FUNC_CMD_CHK_MCU_STA,
    FUNC_CMD_OTA_START,
    FUNC_CMD_OTA_TRANSPORT,
    FUNC_CMD_GET_LOCAL_TIME,
    FUNC_CMD_FACTORY_TEST,
    FUNC_CMD_MID,                   //�����м�ָ�
    FUNC_CMD_UP,
    FUNC_CMD_DOWN,
    FUNC_CMD_POWER,
    FUNC_CMD_SOCKET_POWER,
    FUNC_CMD_END,                   //�������
}FUNC_CMD_TYPE;


enum{
    CMD_RX_HEART_BEAT = 0x00,
    CMD_RX_CHK_PRODUCT_INFO = 0x01,
    CMD_RX_CHK_MCU_MODE = 0x02,
    CMD_RX_WIFI_STA_REPORT = 0x03,
    CMD_RX_WIFI_RESET = 0x04,
    CMD_RX_WIFI_MODE_RESET = 0x05,
    CMD_RX_WIFI_CONFIG = 0x09,
    CMD_RX_CHK_MCU_STA = 0x08,
    CMD_RX_OTA_START = 0x0a,
    CMD_RX_OTA_TRANSPORT = 0x0b,
    CMD_RX_GET_LOCAL_TIME = 0x1c, 
    CMD_RX_FACTORY_TEST = 0x0e,
    CMD_RX_CONTROL = 0x07,
}CMD_RX_TYPE;


enum switch_names
{
    Switch3 = 0,
    Switch2 = 1,
    Switch1 = 2
};


//�ⲿ����
void livolo_switch_set_powerstate(uint8_t index, bool sta);              //�����ܵ�Դ  
void livolo_switch_set_SocketPowerState(bool sta);        //���ò�����Դ
void livolo_switch_set_up(bool sta);                      //���õ������ 
void livolo_switch_set_down(bool sta);                    //���õ���½�
void livolo_switch_set_led_wifi_mode(uint8_t mode);       //����wifiָʾ��
void livolo_switch_set_wifi_config(uint8_t sta);      //����wifi�Ƿ����ñ�־
void livolo_switch_report_wifi_config(uint8_t config);    //����wifi����״̬
void livolo_switch_report_factory_test(int err_code);       //���ͳ�������MCU

bool livolo_switch_get_powerstate(uint8_t index);
bool livolo_switch_get_SocketPowerState(void);
void livolo_switch_get_up(void);                      //���õ������ 
void livolo_switch_get_down(void);                    //���õ���½�
char livolo_switch_get_sta(void);

void livolo_switch_main(void);
void livolo_switch_set_led_mode(uint8_t mode);

void get_mcu_switch_sta(void);

void vendor_product_init(void);

#endif

