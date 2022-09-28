/* ******************************
 * Author : Snow Yang
 * Date   : 2018-12-04
 * Mail   : yangsw@mxchip.com
 * ******************************/

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#include "mcli.h"

#define CMD_MAX_NUM             50
#define CMD_ARG_MAX_NUM         8
#define CMD_BUF_SIZE            256

#define CR_CHAR '\r'
#define LF_CHAR '\n'

static const mcli_cmd_t *mcli_cmds_list[CMD_MAX_NUM];
static uint32_t mcli_cmds_num;
static mcli_if_t *mcli_if;

#ifdef _MICO_INCLUDE_
typedef void (*legacy_cli_func_t) (char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv);
static const mcli_cmd_t *legacy_cli_cmds_list[CMD_MAX_NUM];
static uint32_t legacy_cli_cmds_num=0;
#endif

static void help_cmd_handler(int argc, char *argv[]);

static mcli_cmd_t help_cmd = {
    .name = "help",
    .func = help_cmd_handler,
    .help = "",
};

static char mcli_read_char(void)
{
    return mcli_if->read_char();
}

static void mcli_write(const char *ch, uint32_t n)
{
    while (n--)
    {
        mcli_if->write_char(*ch++);
    }
}

int mcli_cmd_add(mcli_cmd_t *hdl)
{
    int i;

    for (i = 0; i < mcli_cmds_num; i++)
    {
        if (mcli_cmds_list[i] == hdl)
            return 0;
    }
    if (i >= CMD_MAX_NUM)
        return -1;

    mcli_cmds_list[mcli_cmds_num++] = hdl;
    return 0;
}

int mcli_cmd_del(mcli_cmd_t *hdl)
{
    int i;

    for (i = 0; i < mcli_cmds_num; i++)
    {
        if (mcli_cmds_list[i] == hdl)
        {
            mcli_cmds_num--;
            int remain_cmds = mcli_cmds_num - i;
            if (remain_cmds > 0)
            {
                memmove(&mcli_cmds_list[i], &mcli_cmds_list[i + 1], remain_cmds * sizeof(struct mcli_cmd_t *));
            }
            mcli_cmds_list[mcli_cmds_num] = NULL;
            return 0;
        }
    }

    return -1;
}

int mcli_cmds_add(mcli_cmd_t *hdl, uint32_t n)
{
    while (n--)
    {
        if (mcli_cmd_add(hdl++) != 0)
            return -1;
    }

    return 0;
}

int mcli_cmds_del(mcli_cmd_t *hdl, uint32_t n)
{
    while (n--)
    {
        if (mcli_cmd_del(hdl++) != 0)
            return -1;
    }

    return 0;
}

static void help_cmd_handler(int argc, char *argv[])
{
    int i;
    int max_name_len = 0;

    char format[32];
    char split[] = "------------------------------------------------\r\n";

    for (i = 0; i < mcli_cmds_num; i++)
    {
        int name_len = strlen(mcli_cmds_list[i]->name);
        if (name_len > max_name_len)
        {
            max_name_len = name_len;
        }
    }
#ifdef _MICO_INCLUDE_
    for (i = 0; i < legacy_cli_cmds_num; i++)
    {
        int name_len = strlen(legacy_cli_cmds_list[i]->name);
        if (name_len > max_name_len)
        {
            max_name_len = name_len;
        }
    }
#endif

    sprintf(format, "| %%-%ds | %%s\r\n", max_name_len);

    mcli_printf("%s", split);
    mcli_printf(format, "Name", "Description");
    split[0] = '|';
    split[max_name_len + 3] = '|';
    mcli_printf("%s", split);

    for (i = 0; i < mcli_cmds_num; i++)
    {
        mcli_printf(format, mcli_cmds_list[i]->name, mcli_cmds_list[i]->help);
    }
#ifdef _MICO_INCLUDE_
    for (i = 0; i < legacy_cli_cmds_num; i++)
    {
        mcli_printf(format, legacy_cli_cmds_list[i]->name, legacy_cli_cmds_list[i]->help);
    }
#endif
    split[0] = '-';
    split[max_name_len + 3] = '-';
    mcli_printf("%s", split);
}

/*
return code:
argc
*/
static int mcli_args_delim(char *line, char **argv)
{
    int i;

    for (i = 0; i < CMD_ARG_MAX_NUM; i++)
    {
        //if ((argv[i] = strtok(line, " ")) || (argv[i] = strtok(line, '\r')) || (argv[i] = strtok(line, '\n')) == NULL)
        if ((argv[i] = strtok(line, " ")) == NULL)
            break;
        if (i == 0)
            line = NULL;
    }

    return i;
}

static int mcli_parse(int argc, char *argv[])
{
    int ret = 0;

    if (argc == 0)
        return 1;

    for (int i = 0; i < mcli_cmds_num; i++)
    {
        if (strcmp(argv[0], mcli_cmds_list[i]->name) == 0)
        {
            ret = mcli_cmds_list[i]->func(argc, argv);
            return ret;
        }
    }
#ifdef _MICO_INCLUDE_
    for (int i = 0; i < legacy_cli_cmds_num; i++)
    {
        if (strcmp(argv[0], legacy_cli_cmds_list[i]->name) == 0)
        {
            uint8_t *output = malloc(1024);
            int len;
            if (output == NULL)
                len = 0;
            else {
                len = 1024;
                memset(output, 0, 1024);
            }
            
            ((legacy_cli_func_t)legacy_cli_cmds_list[i]->func)(output, len, argc, argv);
            if (output) {
                mcli_printf("%s\r\n", output);
                free(output);
            }
            return;
        }
    }
#endif    
    printf("%s: command not found\r\n", argv[0]);
    mcli_printf("%s: command not found\r\n", argv[0]);

    return 2;
}

/*
return code:
<0: no line
=0: empty line
>0: command line
*/
static int mcli_getline(char *line, int size)
{
    static int i = 0;
    int rc = -1;
    const char *echoback = "\x08 \x08";

    for (;;)
    {
        line[i] = mcli_read_char();

        if (line[i] == LF_CHAR)
            continue;

        if (line[i] == CR_CHAR)
        { /* end of input line */
            line[i] = '\0';
            rc = i;
            goto exit;
        }

        if ((line[i] == 0x08) || /* backspace */
            (line[i] == 0x7f))
        { /* DEL */
            if (i > 0)
            {
                i--;
                mcli_write(echoback, 3);
            }
            continue;
        }

        if (line[i] == '\t')
            continue;

        mcli_write(&line[i], 1);

        if (i++ >= size)
        {
            rc = -1;
            goto exit;
        }
    }

exit:
    mcli_printf("\r\n");
    i = 0;
    return rc;
}

void mcli_init(mcli_if_t *interface)
{
    mcli_if = interface;
    mcli_cmd_add(&help_cmd);
}

int mcli_parse_buf(char *cmd_buf)
{
    char *argv[CMD_ARG_MAX_NUM];

    return mcli_parse(mcli_args_delim(cmd_buf, argv), argv);
}

void mcli_loop_run(void)
{
    char *argv[CMD_ARG_MAX_NUM];
    static char cmd_buf[CMD_BUF_SIZE];
    const char *prompt = "$ ";

    mcli_write(prompt, 2);

    while (1)
    {
        if (mcli_getline(cmd_buf, CMD_BUF_SIZE) >= 0)
        {
            mcli_parse(mcli_args_delim(cmd_buf, argv), argv);
            mcli_write(prompt, 2);
        }
    }
}

void mcli_putc(char ch)
{
    mcli_if->write_char(ch);
}

int mcli_printf(const char *format, ...)
{
    static char printbuf[256];
    va_list ap;

    va_start(ap, format);
    int n = vsnprintf(printbuf, sizeof(printbuf), format, ap);
    va_end(ap);

    if (n > 0)
        mcli_write(printbuf, n);

    return n;
}

#ifdef _MICO_INCLUDE_
/* legacy CLI */
int cli_register_command(const mcli_cmd_t *command)
{
  int i;
  if (!command->name || !command->func)
    return 1;
  
  if (legacy_cli_cmds_num < CMD_MAX_NUM) {
    /* Check if the command has already been registered.
    * Return 0, if it has been registered.
    */
    for (i = 0; i < legacy_cli_cmds_num; i++) {
      if (legacy_cli_cmds_list[i] == command)
        return 0;
    }
    legacy_cli_cmds_list[legacy_cli_cmds_num++] = command;
    return 0;
  }
  
  return 1;
}

int cli_unregister_command(const mcli_cmd_t *command)
{
  int i;
  if (!command->name || !command->func)
    return 1;
  
  for (i = 0; i < legacy_cli_cmds_num; i++) {
    if (legacy_cli_cmds_list[i] == command) {
      legacy_cli_cmds_num--;
      int remaining_cmds = legacy_cli_cmds_num - i;
      if (remaining_cmds > 0) {
        memmove(&legacy_cli_cmds_list[i], &legacy_cli_cmds_list[i + 1],
                (remaining_cmds *
                 sizeof(mcli_cmd_t *)));
      }
      legacy_cli_cmds_list[legacy_cli_cmds_num] = NULL;
      return 0;
    }
  }
  
  return 1;
}


int cli_register_commands(const mcli_cmd_t *commands, int num_commands)
{
  int i;
  for (i = 0; i < num_commands; i++)
    if (cli_register_command(commands++))
      return 1;
  return 0;
}

int cli_unregister_commands(const mcli_cmd_t *commands,
			    int num_commands)
{
  int i;
  for (i = 0; i < num_commands; i++)
    if (cli_unregister_command(commands++))
      return 1;
  
  return 0;
}
#endif
