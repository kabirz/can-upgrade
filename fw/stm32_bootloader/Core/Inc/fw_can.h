/**
 ******************************************************************************
 * @file    fw_can.h
 * @brief   CAN传输层头文件
 * @note    实现基于CAN的固件升级传输层
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026
 *
 ******************************************************************************
 */

#ifndef __FW_CAN_H
#define __FW_CAN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* 导出定义 ---------------------------------------------------------------*/

#define CAN_RING_BUFFER_SIZE    16      /**< CAN环形缓冲区大小 */

/* 导出类型定义 -------------------------------------------------------------*/

/**
 * @brief CAN消息结构体
 */
typedef struct {
    uint32_t id;                         /**< CAN ID */
    uint8_t data[8];                     /**< 数据 */
    uint8_t len;                         /**< 数据长度 */
} fw_can_msg_t;

/* 导出变量 ---------------------------------------------------------------*/

/* 环形缓冲区变量 - 需要在中断中访问 */
extern fw_can_msg_t fw_can_ring_buffer[CAN_RING_BUFFER_SIZE];
extern volatile uint16_t fw_can_ring_head;  /**< 写入位置 */
extern volatile uint16_t fw_can_ring_tail;  /**< 读取位置 */
extern volatile uint8_t fw_can_rx_flag;     /**< 有新消息标志 */

/* 导出函数 ---------------------------------------------------------------*/

/**
 * @brief 向环形缓冲区写入CAN消息（中断中调用）
 * @param id CAN ID
 * @param data 数据指针
 * @param len 数据长度
 * @retval 1-成功 0-缓冲区满
 */
uint8_t FW_CAN_RingBuffer_Write_FromIRQ(uint32_t id, uint8_t *data, uint8_t len);

/**
 * @brief 从环形缓冲区读取CAN消息（主循环中调用）
 * @param id CAN ID指针
 * @param data 数据缓冲区指针
 * @param len 数据长度指针
 * @retval 1-成功 0-缓冲区空
 */
uint8_t FW_CAN_RingBuffer_Read(uint32_t *id, uint8_t *data, uint8_t *len);

/**
 * @brief 初始化CAN传输层
 */
void FW_CAN_Init(void);

/**
 * @brief 处理CAN接收数据（主循环中调用）
 */
void FW_CAN_ProcessRxData(void);

/**
 * @brief 发送CAN响应
 * @param code 响应码
 * @param value 参数值
 */
void FW_CAN_SendResponse(uint32_t code, uint32_t value);

/**
 * @brief 等待CAN发送完成
 */
void FW_CAN_WaitTxComplete(void);

#ifdef __cplusplus
}
#endif

#endif /* __FW_CAN_H */
