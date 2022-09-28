#ifndef _CONFIG_PARAM_H_
#define _CONFIG_PARAM_H_

#define LIGHTBULB_PROJ      1                   //鏅鸿兘鐏?娉?  
#define LED_STRIP_PROJ      2                   //鏅鸿兘鐏?甯?
#define AUTOMATIC_SOCKET    3                   //鏅鸿兘鍗囬檷鎻掑骇
#define LIVOLO_SWITCH       4
#define PROJ_TYPE           LIVOLO_SWITCH      

//LED模式
enum LOG_LEVEL
{
    LOG_LEVEL_NONE,                         //不打印
    LOG_LEVEL_INFO,                         //打印info
    LOG_LEVEL_DEBUG,
};

#define LOG_LEVEL_KV          "log_level_kv"

#endif
