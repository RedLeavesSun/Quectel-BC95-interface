/**
 * Copyright (c), 2012~2017 iot.10086.cn All Rights Reserved
 * @file 		Quectel_BC95.c
 * @brief       从模组收到的数据主要分两类，一是命令的响应（以OK或ERROR结尾）；
 *              二是主动上报的消息（以“+”开头）
 *              发送命令后一定要收到OK或ERROR(1s内可以有回应)
 * @author 		宋伟<songwei1@iot.chinamobile.com>
 * @date 		2017/04/20
 * @version 	1.0.0
 * @par Revision History:
 * 		Date			Author		Revised.Ver		Notes
 * 		2017/04/17		宋伟		  1.0.0			file created
 */

/*****************************************************************************/
/* Includes                                                                  */
/*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Quectel_BC95.h"
#include "plat.h"

/*****************************************************************************/
/* Local Definitions ( Constant and Macro )                                  */
/*****************************************************************************/
#define CMD_RESPONSE_LEN    256
#define NSONMI_INFO_LEN     8
#define NOTIFY_DATA_LEN    (NSONMI_INFO_LEN + 8)

/*****************************************************************************/
/* Structures, Enum and Typedefs                                             */
/*****************************************************************************/
typedef struct Buf
{
    unsigned char *buf;
    unsigned short wr_oft;
    unsigned short len;
} Buf;

typedef struct Quectel_bc95_device
{
    Quectel_BC95_send_callback send_cb;
    struct Buf *recv_buf;
    struct Buf *cmd_response;
    struct Buf *notify_event;
    unsigned int module_status;
    unsigned char received_data_info[8]; /** 字符串格式"socket,length"*/
    unsigned char new_event;
} Quectel_bc95_device;

/*****************************************************************************/
/* Local Function Prototype                                                  */
/*****************************************************************************/

/*****************************************************************************/
/* Local Variables                                                           */
/*****************************************************************************/
static struct Quectel_bc95_device *s_device;

/*****************************************************************************/
/* Global Variables                                                          */
/*****************************************************************************/


/*****************************************************************************/
/* External Functions and Variables                                          */
/*****************************************************************************/



/*****************************************************************************/
/* Function Implementation                                                   */
/*****************************************************************************/
static struct Buf* Buf_malloc(unsigned int len)
{
    struct Buf *tmp = NULL;

    tmp = plat_malloc(sizeof(*tmp));
    if(tmp)
    {
        memset(tmp, 0, sizeof(*tmp));
        tmp->len = len;
        tmp->buf = plat_malloc(len);
        if(NULL == tmp->buf)
        {
            plat_free(tmp);
            tmp = NULL;
        }
        else
        {
            memset(tmp->buf, 0, len);
        }
    }
    return tmp;
}

static void Buf_free(struct Buf *buf)
{
    if(buf)
    {
        if(buf->buf)
            plat_free(buf->buf);
        plat_free(buf);
    }
}

static void Buf_reset(struct Buf *buf)
{
    if(buf && buf->buf)
    {
        memset(buf->buf, 0, buf->len);
        buf->wr_oft = 0;
    }
}

static void HexToStr(char *str, const char *hex, int hex_len)
{
	int i = 0;
	unsigned char tmp = 0;
	
	for(i = 0; i < hex_len; i++)
	{
		tmp = hex[i] >> 4;
		if(tmp > 9)
			*str++ = tmp + 0x37;
		else
			*str++ = tmp + '0';
		
		tmp = hex[i] & 0xF;
		if(tmp > 9)
			*str++ = tmp + 0x37;
		else
			*str++ = tmp + '0';
	}
}

static int StrToHex(char *hex, const char *str)
{
	int hex_len = strlen(str)/2;
	unsigned char tmp_val = 0;
	int i = 0;
	
	for(i = 0; i < hex_len; i++)
	{
		tmp_val = ((str[2 * i] > '9') ? (str[2 * i] - 0x37) : (str[2 * i] - '0'));
		*hex = tmp_val << 4;
		tmp_val = ((str[2 * i + 1] > '9') ? (str[2 * i + 1] - 0x37) : (str[2 * i + 1] - '0'));
		*hex++ |= tmp_val;
	}
	return hex_len;
}

static void event_line_parse(unsigned char *line)
{
    unsigned char *tmp = NULL;
    unsigned char *received_data_info = NULL;

    if(NULL != (tmp = strstr(line, "+CSCON:")))
    {
        if('1' == *(tmp + 7))
            s_device->module_status |= QUECTEL_CSCON_MASK;
        else
            s_device->module_status &= ~QUECTEL_CSCON_MASK;
    }
    else if(NULL != (tmp = strstr(line, "+CEREG:")))
    {
        if('1' == *(tmp + 7))
            s_device->module_status |= QUECTEL_CEREG_MASK;
        else
            s_device->module_status &= ~QUECTEL_CEREG_MASK;
    }
    else if(NULL != (tmp = strstr(line, "+CGATT:")))
    {
        if('1' == *(tmp + 7))
            s_device->module_status |= QUECTEL_CGATT_MASK;
        else
            s_device->module_status &= ~QUECTEL_CGATT_MASK;
    }
    else if(NULL != (tmp = strstr(line, "+NSONMI:")))
    {
        received_data_info = tmp + 8;
        tmp = strstr(tmp, "\r\n");
        memset(s_device->received_data_info, 0, 8);
        memcpy(s_device->received_data_info, received_data_info, tmp - received_data_info);
    }
}

void Quectel_BC95_recv_char(char c)
{
    if(NULL == s_device)
        return;

    if('+' == c)
    {
        /** 有事件，切换缓冲区*/
        Buf_reset(s_device->notify_event);
        s_device->recv_buf = s_device->notify_event;
        s_device->recv_buf->wr_oft = 0;
        s_device->new_event = 1;
    }
    s_device->recv_buf->buf[s_device->recv_buf->wr_oft++] = c;
    s_device->recv_buf->buf[s_device->recv_buf->wr_oft] = '\0';
    s_device->recv_buf->wr_oft %= (s_device->recv_buf->len - 1);

    if('\n' == c)
    {
        /** 行结束*/
        if(s_device->new_event)
        {
            /** 解析事件*/
            event_line_parse(s_device->notify_event->buf);

            /** 事件收完，切换回去*/
            s_device->recv_buf = s_device->cmd_response;
            s_device->new_event = 0;
        }
    }
}

static Quectel_response_type cmd_response_parse(unsigned char **response)
{
    unsigned char *tmp = NULL;
    unsigned char *line = NULL;
    unsigned char *res_end = NULL;
    Quectel_response_type evt = QUECTEL_RESPONSE_NONE;

    if(NULL != (tmp = strstr(s_device->cmd_response->buf, "OK")))
    {
        evt = QUECTEL_RESPONSE_CMD_OK; 
        /** 提取非空行的内容返回*/
        if(response)
        {
            line = s_device->cmd_response->buf;
            res_end = tmp;
            while(NULL != (tmp = strstr(line, "\r\n")))
            {
                if(0 < (tmp - line))
                {
                    /** 非空行*/
                    *response = line;
                    *tmp = '\0';
                    break;
                }

                if(tmp == (res_end - 2))
                {
                    /** 已经找到“OK”前最后一个换行符，直接退出*/
                    break;
                }
                else
                    line = tmp + 2;
            }
        }
    }
    else if(NULL != (tmp = strstr(s_device->cmd_response->buf, "ERROR")))
    {
        evt = QUECTEL_RESPONSE_CMD_ERROR; 
    }

    return evt;
}

// 1 - ok
// 0 - error
// -1 - communication failed or timeout
int Quectel_BC95_send_cmd(unsigned char *cmd, unsigned int timeout, unsigned char **response)
{
    int i = 0;
    Quectel_response_type evt = QUECTEL_RESPONSE_NONE;

    if(s_device->send_cb)
    {
        Buf_reset(s_device->cmd_response);

        s_device->send_cb(cmd, strlen(cmd));

        for(i = 0; i <= timeout/100; i++)
        {
            evt = cmd_response_parse(response);
            if(QUECTEL_RESPONSE_NONE != evt)
            {
                break;
            }
            plat_delay(100);
        }
    }

    return evt;
}

int Quectel_BC95_socket(unsigned short port)
{
    unsigned char cmd_buf[32] = {0};
    unsigned char *response = NULL;
    int fd = -1;

    sprintf(cmd_buf, "AT+NSOCR=DGRAM,17,%d,1\r\n", port);
    if(QUECTEL_RESPONSE_CMD_OK == Quectel_BC95_send_cmd(cmd_buf, 2000, &response))
    {
        fd = atoi(response);
    }

    return fd;
}

int Quectel_BC95_socket_close(int socket)
{
    unsigned char cmd_buf[16] = {0};

    sprintf(cmd_buf, "AT+NSOCL=%d\r\n", socket);
    if(QUECTEL_RESPONSE_CMD_OK == Quectel_BC95_send_cmd(cmd_buf, 2000, NULL))
        return 0;
    else
        return -1;
}

// remote_addr: [ip,port]
int Quectel_BC95_send_data(int socket, unsigned char *remote_addr, const unsigned char *data, unsigned int data_len)
{
    unsigned char *send_buf = NULL;
    unsigned char *response = NULL;
    unsigned char *tmp = NULL;
    int ret = -1;

    if(NULL == s_device->send_cb)
        return ret;
    
    send_buf = plat_malloc(40 + 2 * data_len);
    if(send_buf)
    {
        memset(send_buf, 0, 40 + 2 * data_len);
        sprintf(send_buf, "AT+NSOST=%d,%s,%d,", socket, remote_addr, data_len);
        HexToStr(send_buf + strlen(send_buf), data, data_len);
        send_buf[strlen(send_buf)] = '\r';
        send_buf[strlen(send_buf)] = '\n';

        if(QUECTEL_RESPONSE_CMD_OK == Quectel_BC95_send_cmd(send_buf, 2000, &response))
        {
            if(NULL != (tmp = strstr(response, ",")))
            {
                ret = atoi(tmp + 1);
            }
        }
        free(send_buf);
    }

    return ret;
}

// remote_addr: [ip,port]
// 先假设data空间够，否则需要根据received_data_info里的数据长度和data_buf_len作比较。目前没有定义
// 错误码，暂不比较
int Quectel_BC95_recv_data(unsigned char *remote_addr, unsigned char *data, unsigned int data_buf_len)
{
    unsigned char recv_cmd[16] = {0};
    unsigned int recv_len = 0;
    unsigned char *tmp = NULL;
    unsigned char *tmp1 = NULL;
    unsigned char *response = NULL;

    if(0 < strlen(s_device->received_data_info))
    {
        sprintf(recv_cmd, "AT+NSORF=%s\r\n", s_device->received_data_info);    

        //即时清空，避免在新的中断产生时候清空造成消息遗漏的问题
        memset(s_device->received_data_info, 0, 8);
		
        if(QUECTEL_RESPONSE_CMD_OK == Quectel_BC95_send_cmd(recv_cmd, 5000, &response))
        {
            if(response)
            {
                tmp = strstr(response, ",");
                tmp1 = tmp + 1;
                tmp = strstr(tmp1, ",");
                tmp++;
                tmp = strstr(tmp, ",");
                memcpy(remote_addr, tmp1, tmp - tmp1);
                tmp++;
                tmp = strstr(tmp, ",");
                tmp1 = tmp + 1;
                tmp = strstr(tmp1, ",");
                *tmp = '\0';
                recv_len = StrToHex(data, tmp1);
            }
        }
    }

    return recv_len;
}

int Quectel_BC95_get_link_status(void)
{
    return s_device->module_status;
}

int Quectel_BC95_exit(void)
{
    struct Quectel_bc95_device *tmp_device = s_device;

    s_device = NULL;
    if(tmp_device)
    {
        Buf_free(tmp_device->notify_event);
        Buf_free(tmp_device->cmd_response);
        plat_free(tmp_device);
    }
}

// -1 - failed
int Quectel_BC95_init(Quectel_BC95_send_callback cb)
{
    struct Quectel_bc95_device *tmp_device = NULL;

    if(s_device)
        Quectel_BC95_exit();

    tmp_device = plat_malloc(sizeof(*tmp_device));
    if(NULL == tmp_device)
        return -1;

    memset(tmp_device, 0, sizeof(*tmp_device));
    tmp_device->cmd_response = Buf_malloc(CMD_RESPONSE_LEN);
    if(NULL == tmp_device->cmd_response)
    {
        plat_free(tmp_device);
    }

    tmp_device->notify_event = Buf_malloc(NOTIFY_DATA_LEN);
    if(NULL == tmp_device->notify_event)
    {
        Buf_free(tmp_device->cmd_response);
        plat_free(tmp_device);
        return -1;
    }

    tmp_device->recv_buf = tmp_device->cmd_response;
    tmp_device->send_cb = cb;
    s_device = tmp_device;

#if 0
    ret = Quectel_BC95_send_cmd("AT+CFUN=1\r\n", 2000, NULL);
    ret = Quectel_BC95_send_cmd("AT+CSCON=1\r\n", 2000, NULL);
    ret = Quectel_BC95_send_cmd("AT+CEREG=1\r\n", 2000, NULL);
    ret = Quectel_BC95_send_cmd("AT+CGATT=1\r\n", 2000, NULL);
    ret = Quectel_BC95_send_cmd("AT+CGATT?\r\n", 2000, NULL);
#endif
    return 0;
}

