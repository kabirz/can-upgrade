/**
 ******************************************************************************
 * @file    fw_uart.c
 * @brief   UART传输层实现
 * @note    实现基于UART的固件升级传输层，协议与CAN完全一致
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "fw_uart.h"
#include "fw_upgrade.h"
#include "main.h"
#include "usart.h"
#include <string.h>

/* 私有定义 ---------------------------------------------------------------*/

/* UART环形缓冲区 */
fw_uart_frame_t fw_uart_ring_buffer[UART_RING_BUFFER_SIZE];
volatile uint16_t fw_uart_ring_head = 0;   /* 写入位置 */
volatile uint16_t fw_uart_ring_tail = 0;   /* 读取位置 */
volatile uint8_t fw_uart_rx_flag = 0;      /* 有新帧标志 */

/* 私有变量 ---------------------------------------------------------------*/

static uart_rx_state_t uart_rx_state = UART_STATE_IDLE;
static uint8_t uart_rx_buffer[16];         /* 接收缓冲区 */
static uint8_t uart_rx_index = 0;          /* 接收索引 */
static uint8_t uart_rx_len = 0;            /* 数据长度 */
static uint8_t uart_rx_type = 0;           /* 帧类型 */
static uint16_t uart_rx_crc = 0;           /* 接收的CRC */

/* 私有函数声明 -----------------------------------------------------------*/

static uint16_t UART_CalcCRC16(uint8_t *data, uint16_t len);
static void UART_SendFrame(uint8_t type, uint8_t *data, uint8_t len);

/* 导出函数 ---------------------------------------------------------------*/

/**
 * @brief 向环形缓冲区写入UART帧（中断中调用）
 * @param type 帧类型
 * @param data 数据指针
 * @param len 数据长度
 * @retval 1-成功 0-缓冲区满
 */
uint8_t FW_UART_RingBuffer_Write_FromIRQ(uint8_t type, uint8_t *data, uint8_t len)
{
    uint16_t next_head;

    /* 计算下一个写入位置 */
    next_head = (fw_uart_ring_head + 1) % UART_RING_BUFFER_SIZE;

    /* 检查缓冲区是否已满 */
    if (next_head == fw_uart_ring_tail)
    {
        /* 缓冲区满，丢弃最旧的消息 */
        fw_uart_ring_tail = (fw_uart_ring_tail + 1) % UART_RING_BUFFER_SIZE;
    }

    /* 写入消息 */
    fw_uart_ring_buffer[fw_uart_ring_head].type = type;
    fw_uart_ring_buffer[fw_uart_ring_head].len = len;
    memcpy(fw_uart_ring_buffer[fw_uart_ring_head].data, data, len);

    /* 更新写入位置 */
    fw_uart_ring_head = next_head;

    return 1;
}

/**
 * @brief 从环形缓冲区读取UART帧（主循环中调用）
 * @param type 帧类型指针
 * @param data 数据缓冲区指针
 * @param len 数据长度指针
 * @retval 1-成功 0-缓冲区空
 */
uint8_t FW_UART_RingBuffer_Read(uint8_t *type, uint8_t *data, uint8_t *len)
{
    /* 检查缓冲区是否为空 */
    if (fw_uart_ring_head == fw_uart_ring_tail)
    {
        return 0;
    }

    /* 读取消息 */
    *type = fw_uart_ring_buffer[fw_uart_ring_tail].type;
    *len = fw_uart_ring_buffer[fw_uart_ring_tail].len;
    memcpy(data, fw_uart_ring_buffer[fw_uart_ring_tail].data, *len);

    /* 更新读取位置 */
    fw_uart_ring_tail = (fw_uart_ring_tail + 1) % UART_RING_BUFFER_SIZE;

    return 1;
}

/**
 * @brief 初始化UART传输层
 */
void FW_UART_Init(void)
{
    /* 重置接收状态机 */
    uart_rx_state = UART_STATE_IDLE;
    uart_rx_index = 0;
    uart_rx_len = 0;
    uart_rx_type = 0;
    uart_rx_crc = 0;

    /* 清空环形缓冲区 */
    fw_uart_ring_head = 0;
    fw_uart_ring_tail = 0;
    fw_uart_rx_flag = 0;

    /* 启动UART接收中断 */
    UART_StartRxIT();

    /* 点亮LED2表示进入UART升级模式 */
    HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);
    Log_printf("[FW_UART] UART transport ready (LED2 ON)\r\n");
}

/**
 * @brief 处理UART接收数据（主循环中调用）
 */
void FW_UART_ProcessRxData(void)
{
    if (!fw_uart_rx_flag)
    {
        return;
    }

    fw_uart_rx_flag = 0;

    /* 从环形缓冲区读取所有积压的帧 */
    uint8_t type;
    uint8_t data[8];
    uint8_t len;

    while (FW_UART_RingBuffer_Read(&type, data, &len))
    {
        if (type == UART_FRAME_CMD)
        {
            /* 处理命令 */
            Log_printf("[UART] CMD: type=%d, len=%d\r\n", type, len);
            FW_ProcessCommand(data, len);
        }
        else if (type == UART_FRAME_DATA)
        {
            /* 处理固件数据 */
            FW_ProcessFirmwareData(data, len);
        }
    }
}

/**
 * @brief 发送UART响应
 * @param code 响应码
 * @param value 参数值
 */
void FW_UART_SendResponse(uint32_t code, uint32_t value)
{
    uint8_t response_data[8];

    /* 准备响应数据: 2个uint32_t，与CAN格式完全一致 */
    memcpy(&response_data[0], &code, 4);
    memcpy(&response_data[4], &value, 4);

    /* 发送响应帧 */
    UART_SendFrame(UART_FRAME_CMD, response_data, 8);

    if (code != FW_CODE_OFFSET)  /* 避免打印过多日志 */
    {
        Log_printf("[UART] TX: code=%d, value=0x%08X\r\n", code, value);
    }
}

/**
 * @brief 等待UART发送完成
 */
void FW_UART_WaitTxComplete(void)
{
    /* 等待UART发送完成 */
    HAL_UART_Transmit(&huart1, (uint8_t*)"", 0, 100);  /* 触发等待 */
}

/**
 * @brief UART接收回调（中断中调用）
 * @param byte 接收到的字节
 */
void FW_UART_RxCallback(uint8_t byte)
{
    switch (uart_rx_state)
    {
        case UART_STATE_IDLE:
            if (byte == UART_FRAME_HEAD)
            {
                uart_rx_state = UART_STATE_TYPE;
                uart_rx_index = 0;
            }
            break;

        case UART_STATE_TYPE:
            if (byte == UART_FRAME_CMD || byte == UART_FRAME_DATA)
            {
                uart_rx_type = byte;
                uart_rx_state = UART_STATE_LEN_H;
            }
            else
            {
                uart_rx_state = UART_STATE_IDLE;
            }
            break;

        case UART_STATE_LEN_H:
            uart_rx_len = byte << 8;
            uart_rx_state = UART_STATE_LEN_L;
            break;

        case UART_STATE_LEN_L:
            uart_rx_len |= byte;
            /* 验证长度 */
            if (uart_rx_len <= 8)
            {
                if (uart_rx_len > 0)
                {
                    uart_rx_state = UART_STATE_DATA;
                }
                else
                {
                    /* 长度为0，直接跳到CRC */
                    uart_rx_state = UART_STATE_CRC_H;
                }
            }
            else
            {
                uart_rx_state = UART_STATE_IDLE;
            }
            break;

        case UART_STATE_DATA:
            uart_rx_buffer[uart_rx_index++] = byte;
            if (uart_rx_index >= uart_rx_len)
            {
                uart_rx_state = UART_STATE_CRC_H;
            }
            break;

        case UART_STATE_CRC_H:
            uart_rx_crc = byte << 8;
            uart_rx_state = UART_STATE_CRC_L;
            break;

        case UART_STATE_CRC_L:
            uart_rx_crc |= byte;
            /* 校验CRC */
            {
                uint16_t calc_crc = UART_CalcCRC16(uart_rx_buffer, uart_rx_len);
                if (calc_crc == uart_rx_crc)
                {
                    /* CRC正确，写入环形缓冲区 */
                    FW_UART_RingBuffer_Write_FromIRQ(uart_rx_type, uart_rx_buffer, uart_rx_len);
                    fw_uart_rx_flag = 1;
                }
            }
            uart_rx_state = UART_STATE_TAIL;
            break;

        case UART_STATE_TAIL:
            if (byte == UART_FRAME_TAIL)
            {
                /* 帧接收完成 */
            }
            uart_rx_state = UART_STATE_IDLE;
            break;

        default:
            uart_rx_state = UART_STATE_IDLE;
            break;
    }
}

/* 私有函数 ---------------------------------------------------------------*/

/**
 * @brief 计算CRC16-CCITT
 * @param data 数据指针
 * @param len 数据长度
 * @retval CRC16值
 */
static uint16_t UART_CalcCRC16(uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t i;

    for (i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

/**
 * @brief 发送UART帧
 * @param type 帧类型
 * @param data 数据指针
 * @param len 数据长度
 */
static void UART_SendFrame(uint8_t type, uint8_t *data, uint8_t len)
{
    uint8_t frame[32];
    uint16_t crc;
    uint16_t index = 0;

    /* 帧头 */
    frame[index++] = UART_FRAME_HEAD;

    /* 帧类型 */
    frame[index++] = type;

    /* 长度 */
    frame[index++] = (len >> 8) & 0xFF;
    frame[index++] = len & 0xFF;

    /* 数据 */
    memcpy(&frame[index], data, len);
    index += len;

    /* CRC */
    crc = UART_CalcCRC16(data, len);
    frame[index++] = (crc >> 8) & 0xFF;
    frame[index++] = crc & 0xFF;

    /* 帧尾 */
    frame[index++] = UART_FRAME_TAIL;

    /* 发送 */
    HAL_UART_Transmit(&huart1, frame, index, 1000);
}
