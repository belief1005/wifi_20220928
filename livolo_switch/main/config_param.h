#ifndef _CONFIG_PARAM_H_
#define _CONFIG_PARAM_H_

#define LIGHTBULB_PROJ      1                   //智能�?�?  
#define LED_STRIP_PROJ      2                   //智能�?�?
#define AUTOMATIC_SOCKET    3                   //智能升降插座
#define LIVOLO_SWITCH       4
#define PROJ_TYPE           LIVOLO_SWITCH      

//LEDģʽ
enum LOG_LEVEL
{
    LOG_LEVEL_NONE,                         //����ӡ
    LOG_LEVEL_INFO,                         //��ӡinfo
    LOG_LEVEL_DEBUG,
};

#define LOG_LEVEL_KV          "log_level_kv"

#endif
