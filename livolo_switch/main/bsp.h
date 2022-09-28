#ifndef _BSP_H__
#define _BSP_H__

enum ERR_CODE
{
    ERR_CODE_NONE,
    ERR_CODE_NO_AUTH_CODE,
    ERR_CODE_NO_FACTORY_SSID,
    ERR_CODE_BOTH,
};

enum TEST_CODE
{
    FACTORY_TEST_NONE,
    FACTORY_TEST_AUTH_CODE_OK,
    FACTORY_TEST_FACTORY_SSID_OK,
    FACTORY_TEST_BOTH,
};

int8_t bsp_report_wifi_rssi(void);
void bsp_scan_done_handler(void);
void bsp_init(void);

#endif