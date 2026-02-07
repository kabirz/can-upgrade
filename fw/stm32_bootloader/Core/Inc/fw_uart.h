/**
 ******************************************************************************
 * @file    fw_uart.h
 * @brief   UART传输层头文件
 * @note    实现基于UART的固件升级传输层，协议与CAN完全一致
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026
 *
 ******************************************************************************
 */

#ifndef __FW_UART_H
#define __FW_UART_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* 导出定义 ---------------------------------------------------------------*/

#define UART_RING_BUFFER_SIZE    128     /**< UART环形缓冲区大小 */

/* UART协议帧格式定义 */
#define UART_FRAME_HEAD          0xAA    /**< 帧头 */
#define UART_FRAME_TAIL          0x55    /**< 帧尾 */
#define UART_FRAME_CMD           0x01    /**< 命令帧类型 */
#define UART_FRAME_DATA          0x02    /**< 数据帧类型 */

/* 导出类型定义 -------------------------------------------------------------*/

/**
 * @brief UART帧状态枚举
 */
typedef enum {
    UART_STATE_IDLE = 0,        /**< 空闲状态 */
    UART_STATE_HEAD,            /**< 等待帧头 */
    UART_STATE_TYPE,            /**< 等待帧类型 */
    UART_STATE_LEN_H,           /**< 等待长度高字节 */
    UART_STATE_LEN_L,           /**< 等待长度低字节 */
    UART_STATE_DATA,            /**< 接收数据中 */
    UART_STATE_CRC_H,           /**< 等待CRC高字节 */
    UART_STATE_CRC_L,           /**< 等待CRC低字节 */
    UART_STATE_TAIL             /**< 等待帧尾 */
} uart_rx_state_t;

/**
 * @brief UART帧结构体
 */
typedef struct {
    uint8_t type;               /**< 帧类型: CMD/DATA */
    uint8_t data[8];            /**< 数据 */
    uint8_t len;                /**< 数据长度 */
} fw_uart_frame_t;

/* 导出变量 ---------------------------------------------------------------*/

/* 环形缓冲区变量 - 需要在中断中访问 */
extern fw_uart_frame_t fw_uart_ring_buffer[UART_RING_BUFFER_SIZE];
extern volatile uint16_t fw_uart_ring_head;   /**< 写入位置 */
extern volatile uint16_t fw_uart_ring_tail;   /**< 读取位置 */
extern volatile uint8_t fw_uart_rx_flag;      /**< 有新帧标志 */

/* 导出函数 ---------------------------------------------------------------*/

/**
 * @brief 向环形缓冲区写入UART帧（中断中调用）
 * @param type 帧类型
 * @param data 数据指针
 * @param len 数据长度
 * @retval 1-成功 0-缓冲区满
 */
uint8_t FW_UART_RingBuffer_Write_FromIRQ(uint8_t type, uint8_t *data, uint8_t len);

/**
 * @brief 从环形缓冲区读取UART帧（主循环中调用）
 * @param type 帧类型指针
 * @param data 数据缓冲区指针
 * @param len 数据长度指针
 * @retval 1-成功 0-缓冲区空
 */
uint8_t FW_UART_RingBuffer_Read(uint8_t *type, uint8_t *data, uint8_t *len);

/**
 * @brief 初始化UART传输层
 */
void FW_UART_Init(void);

/**
 * @brief 处理UART接收数据（主循环中调用）
 */
void FW_UART_ProcessRxData(void);

/**
 * @brief 发送UART响应
 * @param code 响应码
 * @param value 参数值
 */
void FW_UART_SendResponse(uint32_t code, uint32_t value);

/**
 * @brief 等待UART发送完成
 */
void FW_UART_WaitTxComplete(void);

/**
 * @brief UART接收回调（中断中调用）
 * @param byte 接收到的字节
 */
void FW_UART_RxCallback(uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* __FW_UART_H */
