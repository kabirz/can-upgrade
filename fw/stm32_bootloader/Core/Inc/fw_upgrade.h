/**
 ******************************************************************************
 * @file    fw_upgrade.h
 * @brief   固件升级框架头文件
 * @note    提供传输层抽象接口，支持CAN/UART等多种升级方式
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026
 *
 ******************************************************************************
 */

#ifndef __FW_UPGRADE_H
#define __FW_UPGRADE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* 导出类型定义 -------------------------------------------------------------*/

/**
 * @brief 传输层接口结构体
 * @note  所有传输方式（CAN/UART等）都需要实现此接口
 */
typedef struct {
    const char *name;                    /**< 传输方式名称，如"CAN"、"UART" */
    void (*init)(void);                  /**< 初始化传输层 */
    void (*process_rx_data)(void);       /**< 处理接收数据（主循环中调用） */
    void (*send_response)(uint32_t code, uint32_t value); /**< 发送响应 */
    void (*wait_tx_complete)(void);      /**< 等待发送完成 */
} fw_transport_t;

/* 导出函数 ---------------------------------------------------------------*/

/**
 * @brief 初始化固件升级模块
 * @param transport 传输层接口指针
 * @retval 0-失败 1-成功
 */
uint8_t FW_Upgrade_Init(const fw_transport_t *transport);

/**
 * @brief 处理固件数据
 * @param data 接收到的数据
 * @param len 数据长度
 * @note 由传输层在接收到固件数据时调用
 */
void FW_ProcessFirmwareData(uint8_t *data, uint8_t len);

/**
 * @brief 处理命令数据
 * @param data 接收到的数据
 * @param len 数据长度
 * @note 由传输层在接收到命令时调用
 */
void FW_ProcessCommand(uint8_t *data, uint8_t len);

/**
 * @brief 启动固件升级流程
 * @retval 0-失败 1-成功
 */
uint8_t FW_StartUpgrade(void);

/**
 * @brief 获取升级状态
 * @retval 0-未升级中 1-升级中
 */
uint8_t FW_IsUpgrading(void);

/**
 * @brief 等待所有数据发送完成
 * @note 用于在跳转应用前确保所有响应都已发送
 */
void FW_WaitTxComplete(void);

/**
 * @brief 获取传输层接口
 * @retval 传输层接口指针
 */
const fw_transport_t* FW_GetTransport(void);

#ifdef __cplusplus
}
#endif

#endif /* __FW_UPGRADE_H */
