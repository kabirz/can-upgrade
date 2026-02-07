/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
/* 固件升级框架函数 */
uint8_t VerifyAppFirmware(void);
uint8_t CheckUpgradeFlag(void);
void ClearUpgradeFlag(void);
void JumpToApp(uint32_t app_addr);
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define Key1_Pin GPIO_PIN_13
#define Key1_GPIO_Port GPIOA
#define Key2_Pin GPIO_PIN_15
#define Key2_GPIO_Port GPIOA
#define LED1_Pin GPIO_PIN_11
#define LED1_GPIO_Port GPIOC
#define LED2_Pin GPIO_PIN_12
#define LED2_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */
/* Flash地址定义 */
#define FLASH_APP_START_ADDR      0x08010000  /* 应用固件起始地址(64KB偏移) */
#define FLASH_APP_END_ADDR        0x08040000  /* 应用固件结束地址(256KB Flash) */
#define FLASH_SECTOR_SIZE         0x400       /* STM32F1页大小:1KB */
#define FLASH_FLAG_ADDR           0x0803F800  /* 升级标志地址(最后一个2KB block起始) */
#define UPGRADE_FLAG_VALUE        0x55AADEAD  /* 升级标志值 */

/* CAN协议ID定义 */
#define CAN_ID_PLATFORM_RX        0x101       /* 接收上位机命令 */
#define CAN_ID_PLATFORM_TX        0x102       /* 发送给上位机响应 */
#define CAN_ID_FW_DATA_RX         0x103       /* 接收固件数据 */

/* 固件命令码 */
#define FW_CMD_START_UPDATE       0           /* 开始升级 */
#define FW_CMD_CONFIRM            1           /* 确认升级 */
#define FW_CMD_VERSION            2           /* 获取版本 */
#define FW_CMD_REBOOT             3           /* 重启 */

/* 固件响应码 */
#define FW_CODE_OFFSET            0           /* 偏移响应 */
#define FW_CODE_UPDATE_SUCCESS    1           /* 升级成功 */
#define FW_CODE_VERSION           2           /* 版本响应 */
#define FW_CODE_CONFIRM           3           /* 确认响应 */
#define FW_CODE_FLASH_ERROR       4           /* Flash错误 */
#define FW_CODE_TRANFER_ERROR     5           /* 传输错误 */

/* 应用固件跳转地址 */
#define APP_START_ADDR            0x08010000

/* Bootloader版本 */
#define BOOTLOADER_VERSION        ((1 << 24) | (0 << 16) | (0 << 8)) /* v1.0.0 */
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
