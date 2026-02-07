/**
 ******************************************************************************
 * @file    fw_can.c
 * @brief   CAN传输层实现
 * @note    实现基于CAN的固件升级传输层
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "fw_can.h"
#include "fw_upgrade.h"
#include "main.h"
#include "can.h"
#include "usart.h"
#include <string.h>

/* 私有定义 ---------------------------------------------------------------*/

/* CAN环形缓冲区 */
fw_can_msg_t fw_can_ring_buffer[CAN_RING_BUFFER_SIZE];
volatile uint16_t fw_can_ring_head = 0;  /* 写入位置 */
volatile uint16_t fw_can_ring_tail = 0;  /* 读取位置 */
volatile uint8_t fw_can_rx_flag = 0;     /* 有新消息标志 */

/* 私有函数声明 -----------------------------------------------------------*/

/* 导出函数 ---------------------------------------------------------------*/

/**
 * @brief 向环形缓冲区写入CAN消息（中断中调用）
 * @param id CAN ID
 * @param data 数据指针
 * @param len 数据长度
 * @retval 1-成功 0-缓冲区满
 */
uint8_t FW_CAN_RingBuffer_Write_FromIRQ(uint32_t id, uint8_t *data, uint8_t len)
{
    uint16_t next_head;

    /* 计算下一个写入位置 */
    next_head = (fw_can_ring_head + 1) % CAN_RING_BUFFER_SIZE;

    /* 检查缓冲区是否已满 */
    if (next_head == fw_can_ring_tail)
    {
        /* 缓冲区满，丢弃最旧的消息 */
        fw_can_ring_tail = (fw_can_ring_tail + 1) % CAN_RING_BUFFER_SIZE;
    }

    /* 写入消息 */
    fw_can_ring_buffer[fw_can_ring_head].id = id;
    fw_can_ring_buffer[fw_can_ring_head].len = len;
    for (uint8_t i = 0; i < len; i++)
    {
        fw_can_ring_buffer[fw_can_ring_head].data[i] = data[i];
    }

    /* 更新写入位置 */
    fw_can_ring_head = next_head;

    return 1;
}

/**
 * @brief 从环形缓冲区读取CAN消息（主循环中调用）
 * @param id CAN ID指针
 * @param data 数据缓冲区指针
 * @param len 数据长度指针
 * @retval 1-成功 0-缓冲区空
 */
uint8_t FW_CAN_RingBuffer_Read(uint32_t *id, uint8_t *data, uint8_t *len)
{
    /* 检查缓冲区是否为空 */
    if (fw_can_ring_head == fw_can_ring_tail)
    {
        return 0;
    }

    /* 读取消息 */
    *id = fw_can_ring_buffer[fw_can_ring_tail].id;
    *len = fw_can_ring_buffer[fw_can_ring_tail].len;
    for (uint8_t i = 0; i < *len; i++)
    {
        data[i] = fw_can_ring_buffer[fw_can_ring_tail].data[i];
    }

    /* 更新读取位置 */
    fw_can_ring_tail = (fw_can_ring_tail + 1) % CAN_RING_BUFFER_SIZE;

    return 1;
}

/**
 * @brief 初始化CAN传输层
 */
void FW_CAN_Init(void)
{
    CAN_FilterTypeDef can_filter;

    /* 配置CAN过滤器 - 接受0x101和0x103 */
    can_filter.FilterBank = 0;
    can_filter.FilterMode = CAN_FILTERMODE_IDLIST;
    can_filter.FilterScale = CAN_FILTERSCALE_16BIT;
    can_filter.FilterIdHigh = (CAN_ID_PLATFORM_RX << 5);
    can_filter.FilterIdLow = (CAN_ID_FW_DATA_RX << 5);
    can_filter.FilterMaskIdHigh = 0;
    can_filter.FilterMaskIdLow = 0;
    can_filter.FilterFIFOAssignment = CAN_RX_FIFO1;
    can_filter.FilterActivation = ENABLE;
    can_filter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan, &can_filter) != HAL_OK)
    {
        Log_printf("[ERROR] CAN filter config failed!\r\n");
        return;
    }

    if (HAL_CAN_Start(&hcan) != HAL_OK)
    {
        Log_printf("[ERROR] CAN start failed!\r\n");
        return;
    }

    if (HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO1_MSG_PENDING) != HAL_OK)
    {
        Log_printf("[ERROR] CAN IRQ enable failed!\r\n");
        return;
    }

    /* 点亮LED1表示进入升级模式 */
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
    Log_printf("[FW_CAN] CAN transport ready (LED1 ON)\r\n");
}

/**
 * @brief 处理CAN接收数据（主循环中调用）
 */
void FW_CAN_ProcessRxData(void)
{
    if (!fw_can_rx_flag)
    {
        return;
    }

    fw_can_rx_flag = 0;

    /* 从环形缓冲区读取所有积压的消息 */
    uint32_t id;
    uint8_t data[8];
    uint8_t len;

    while (FW_CAN_RingBuffer_Read(&id, data, &len))
    {
        if (id == CAN_ID_PLATFORM_RX)
        {
            /* 处理命令 */
            Log_printf("[CAN] CMD: ID=%03X, len=%d\r\n", id, len);
            FW_ProcessCommand(data, len);
        }
        else if (id == CAN_ID_FW_DATA_RX)
        {
            /* 处理固件数据 */
            FW_ProcessFirmwareData(data, len);
        }
    }
}

/**
 * @brief 发送CAN响应
 * @param code 响应码
 * @param value 参数值
 */
void FW_CAN_SendResponse(uint32_t code, uint32_t value)
{
    CAN_TxHeaderTypeDef tx_header;
    uint8_t tx_data[8];
    uint32_t tx_mailbox;

    /* 准备响应数据: 2个uint32_t */
    memcpy(&tx_data[0], &code, 4);
    memcpy(&tx_data[4], &value, 4);

    /* 配置CAN发送头 */
    tx_header.StdId = CAN_ID_PLATFORM_TX;
    tx_header.ExtId = 0;
    tx_header.IDE = CAN_ID_STD;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.DLC = 8;
    tx_header.TransmitGlobalTime = DISABLE;

    /* 等待上一次发送完成 */
    while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0);

    /* 发送消息 */
    if (HAL_CAN_AddTxMessage(&hcan, &tx_header, tx_data, &tx_mailbox) != HAL_OK)
    {
        Log_printf("[ERROR] CAN send failed!\r\n");
        Error_Handler();
    }

    if (code != FW_CODE_OFFSET)  /* 避免打印过多日志 */
    {
        Log_printf("[CAN] TX: code=%d, value=0x%08X\r\n", code, value);
    }
}

/**
 * @brief 等待CAN发送完成
 */
void FW_CAN_WaitTxComplete(void)
{
    /* 等待所有3个邮箱空闲 */
    while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) != 3);
}
