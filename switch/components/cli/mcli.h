/* ******************************
 * Author : Snow Yang
 * Date   : 2018-12-04
 * Mail   : yangsw@mxchip.com
 * ******************************/

#ifndef _MCLI_H
#define _MCLI_H

#include <stdint.h>
#include <string.h>

typedef struct
{
    void (*write_char)(char ch);
    char (*read_char)(void);
} mcli_if_t;

typedef struct
{
    const char *name;
    const char *help;
    int (*func)(int argc, char *argv[]);
} mcli_cmd_t;

void mcli_init(mcli_if_t *interface);
void mcli_loop_run(void);
int mcli_parse_buf(char *cmd_buf);
int mcli_cmd_add(mcli_cmd_t *hdl);
int mcli_cmd_del(mcli_cmd_t *hdl);
int mcli_cmds_add(mcli_cmd_t *hdl, uint32_t n);
int mcli_cmds_del(mcli_cmd_t *hdl, uint32_t n);
void mcli_putc(char ch);
int mcli_printf(const char *format, ...);

#endif