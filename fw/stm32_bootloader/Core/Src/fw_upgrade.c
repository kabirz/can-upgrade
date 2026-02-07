/**
 ******************************************************************************
 * @file    fw_upgrade.c
 * @brief   固件升级核心逻辑
 * @note    提供传输层无关的固件升级功能
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "fw_upgrade.h"
#include "main.h"
#include "usart.h"
#include <string.h>

/* 私有定义 ---------------------------------------------------------------*/

#define FLASH_BUFFER_SIZE      64    /**< Flash写入缓冲区大小 */

/* 私有变量 ---------------------------------------------------------------*/

static const fw_transport_t *g_transport = NULL;  /**< 当前传输层 */

/* 外部声明 */
extern CAN_HandleTypeDef hcan;
extern UART_HandleTypeDef huart1;

/* Flash写入缓冲区 */
static uint8_t flash_buffer[FLASH_BUFFER_SIZE];
static uint16_t flash_buffer_index = 0;
static uint32_t current_flash_addr = FLASH_APP_START_ADDR;

/* 固件接收状态 */
static uint32_t total_fw_size = 0;
static uint32_t received_fw_size = 0;
static uint8_t is_upgrading = 0;

/* 私有函数声明 -----------------------------------------------------------*/

static HAL_StatusTypeDef Flash_EraseAppArea(uint32_t firmware_size);
static HAL_StatusTypeDef Flash_WriteData(uint32_t start_addr, uint8_t *data, uint32_t len);
static uint32_t Flash_ReadWord(uint32_t addr);

/* 导出函数 ---------------------------------------------------------------*/

/**
 * @brief 初始化固件升级模块
 * @param transport 传输层接口指针
 * @retval 0-失败 1-成功
 */
uint8_t FW_Upgrade_Init(const fw_transport_t *transport)
{
    if (transport == NULL)
    {
        Log_printf("[FW_UP] Invalid transport!\r\n");
        return 0;
    }

    g_transport = transport;

    Log_printf("[FW_UP] Init with transport: %s\r\n", transport->name);

    /* 初始化传输层 */
    if (g_transport->init != NULL)
    {
        g_transport->init();
    }

    return 1;
}

/**
 * @brief 处理固件数据
 * @param data 接收到的数据
 * @param len 数据长度
 * @note 由传输层在接收到固件数据时调用
 */
void FW_ProcessFirmwareData(uint8_t *data, uint8_t len)
{
    if (received_fw_size >= total_fw_size)
    {
        Log_printf("[WARNING] Extra data: recv=%d, total=%d\r\n", received_fw_size, total_fw_size);
        return;
    }

    /* 将数据写入缓冲区 */
    for (uint8_t i = 0; i < len && received_fw_size < total_fw_size; i++)
    {
        flash_buffer[flash_buffer_index++] = data[i];
        received_fw_size++;

        /* 缓冲区满或接收完成，写入Flash */
        if (flash_buffer_index >= FLASH_BUFFER_SIZE || received_fw_size >= total_fw_size)
        {
            if (Flash_WriteData(current_flash_addr, flash_buffer, flash_buffer_index) != HAL_OK)
            {
                Log_printf("[ERROR] Flash write failed at %08X\r\n", current_flash_addr);
                if (g_transport->send_response != NULL)
                {
                    g_transport->send_response(FW_CODE_FLASH_ERROR, received_fw_size);
                }
                return;
            }

            current_flash_addr += flash_buffer_index;
            flash_buffer_index = 0;

            /* 每4096字节打印一次进度 */
            if (received_fw_size % 4096 == 0 || received_fw_size >= total_fw_size)
            {
                Log_printf("[FLASH] Progress: %d/%d bytes\r\n", received_fw_size, total_fw_size);
            }

            /* 发送响应 */
            if (received_fw_size >= total_fw_size)
            {
                Log_printf("[FLASH] Firmware write complete!\r\n");
                is_upgrading = 0;  /* 升级完成 */
                if (g_transport->send_response != NULL)
                {
                    g_transport->send_response(FW_CODE_UPDATE_SUCCESS, received_fw_size);
                }
            }
            else
            {
                if (g_transport->send_response != NULL)
                {
                    g_transport->send_response(FW_CODE_OFFSET, received_fw_size);
                }
            }
        }
    }
}

/**
 * @brief 处理命令数据
 * @param data 接收到的数据
 * @param len 数据长度
 * @note 由传输层在接收到命令时调用
 */
void FW_ProcessCommand(uint8_t *data, uint8_t len)
{
    uint32_t cmd, param;

    if (len < 8)
    {
        Log_printf("[CMD] Invalid data length: %d\r\n", len);
        return;
    }

    /* 解析命令 */
    memcpy(&cmd, &data[0], 4);
    memcpy(&param, &data[4], 4);

    Log_printf("[CMD] Processing: cmd=%d, param=%08X\r\n", cmd, param);

    switch (cmd)
    {
        case FW_CMD_START_UPDATE:
        {
            /* 开始升级，param为固件总大小 */
            if (Flash_EraseAppArea(param) == HAL_OK)
            {
                total_fw_size = param;
                received_fw_size = 0;
                current_flash_addr = FLASH_APP_START_ADDR;
                flash_buffer_index = 0;
                is_upgrading = 1;

                Log_printf("[CMD] Start update: firmware size = %d bytes\r\n", total_fw_size);

                if (g_transport->send_response != NULL)
                {
                    g_transport->send_response(FW_CODE_OFFSET, 0);
                }
                HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
            }
            else
            {
                Log_printf("[ERROR] Flash erase failed!\r\n");
                if (g_transport->send_response != NULL)
                {
                    g_transport->send_response(FW_CODE_FLASH_ERROR, 0);
                }
            }
            break;
        }

        case FW_CMD_CONFIRM:
        {
            /* 确认升级，param为1则启动应用 */
            Log_printf("[CMD] Confirm upgrade: param=%d\r\n", param);

            if (param == 1)
            {
                /* 验证固件 */
                if (VerifyAppFirmware())
                {
                    Log_printf("[CMD] Firmware verified OK, jumping to app...\r\n");
                    if (g_transport->send_response != NULL)
                    {
                        g_transport->send_response(FW_CODE_CONFIRM, 0x55AA55AA);
                    }
                    /* 等待发送完成 */
                    FW_WaitTxComplete();
                    /* 跳转到应用 */
                    JumpToApp(APP_START_ADDR);
                }
                else
                {
                    Log_printf("[ERROR] Firmware verify failed!\r\n");
                    if (g_transport->send_response != NULL)
                    {
                        g_transport->send_response(FW_CODE_FLASH_ERROR, 0);
                    }
                }
            }
            else
            {
                /* 测试模式，不启动应用 */
                Log_printf("[CMD] Test mode, not starting app\r\n");
                if (g_transport->send_response != NULL)
                {
                    g_transport->send_response(FW_CODE_CONFIRM, 0x55AA55AA);
                }
            }
            break;
        }

        case FW_CMD_VERSION:
        {
            /* 返回bootloader版本 */
            Log_printf("[CMD] Get version\r\n");
            if (g_transport->send_response != NULL)
            {
                g_transport->send_response(FW_CODE_VERSION, BOOTLOADER_VERSION);
            }
            break;
        }

        case FW_CMD_REBOOT:
        {
            /* 重启系统 */
            Log_printf("[CMD] Reboot system...\r\n");
            HAL_Delay(100);
            NVIC_SystemReset();
            break;
        }

        default:
            Log_printf("[CMD] Unknown command: %d\r\n", cmd);
            break;
    }
}

/**
 * @brief 启动固件升级流程
 * @retval 0-失败 1-成功
 */
uint8_t FW_StartUpgrade(void)
{
    is_upgrading = 0;
    total_fw_size = 0;
    received_fw_size = 0;
    flash_buffer_index = 0;
    current_flash_addr = FLASH_APP_START_ADDR;

    if (g_transport != NULL && g_transport->init != NULL)
    {
        g_transport->init();
        return 1;
    }

    return 0;
}

/**
 * @brief 获取升级状态
 * @retval 0-未升级中 1-升级中
 */
uint8_t FW_IsUpgrading(void)
{
    return is_upgrading;
}

/**
 * @brief 等待所有数据发送完成
 * @note 用于在跳转应用前确保所有响应都已发送
 */
void FW_WaitTxComplete(void)
{
    if (g_transport != NULL && g_transport->wait_tx_complete != NULL)
    {
        g_transport->wait_tx_complete();
    }
}

/**
 * @brief 获取传输层接口
 * @retval 传输层接口指针
 */
const fw_transport_t* FW_GetTransport(void)
{
    return g_transport;
}

/* 私有函数 ---------------------------------------------------------------*/

/**
 * @brief 擦除应用固件区域（根据固件大小）
 * @param firmware_size 固件大小（字节）
 * @retval HAL状态
 */
static HAL_StatusTypeDef Flash_EraseAppArea(uint32_t firmware_size)
{
    uint32_t page_error = 0;
    FLASH_EraseInitTypeDef erase_init;
    HAL_StatusTypeDef status;
    uint32_t nb_pages;

    /* 计算需要擦除的页数（向上取整） */
    nb_pages = (firmware_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;

    /* 限制最大擦除页数 */
    if (nb_pages > (FLASH_APP_END_ADDR - FLASH_APP_START_ADDR) / FLASH_SECTOR_SIZE)
    {
        nb_pages = (FLASH_APP_END_ADDR - FLASH_APP_START_ADDR) / FLASH_SECTOR_SIZE;
    }

    Log_printf("[FLASH] Erasing: %d bytes (%d pages)\r\n", firmware_size, nb_pages);

    /* 解锁Flash */
    HAL_FLASH_Unlock();

    /* 配置擦除参数 */
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.Banks = FLASH_BANK_1;
    erase_init.PageAddress = FLASH_APP_START_ADDR;
    erase_init.NbPages = nb_pages;

    /* 执行擦除 */
    status = HAL_FLASHEx_Erase(&erase_init, &page_error);

    /* 锁定Flash */
    HAL_FLASH_Lock();

    if (status == HAL_OK)
    {
        Log_printf("[FLASH] Erase success\r\n");
    }
    else
    {
        Log_printf("[ERROR] Erase failed at page %d\r\n", page_error);
    }

    return status;
}

/**
 * @brief 写入数据到Flash
 * @param start_addr 起始地址
 * @param data 数据指针
 * @param len 数据长度
 * @retval HAL状态
 */
static HAL_StatusTypeDef Flash_WriteData(uint32_t start_addr, uint8_t *data, uint32_t len)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint32_t addr = start_addr;
    uint32_t i;
    uint64_t data64 = 0;

    HAL_FLASH_Unlock();

    for (i = 0; i < len; i += 8)
    {
        /* 准备8字节数据 */
        if (i + 8 <= len)
        {
            memcpy(&data64, &data[i], 8);
        }
        else
        {
            /* 不足8字节填充0xFF */
            uint8_t temp[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            memcpy(temp, &data[i], len - i);
            memcpy(&data64, temp, 8);
        }

        /* 写入Flash (每次写入8字节=2个字) */
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, data64);
        if (status != HAL_OK)
        {
            break;
        }
        addr += 8;
    }

    HAL_FLASH_Lock();
    return status;
}

/**
 * @brief 从Flash读取一个字
 * @param addr 地址
 * @retval 读取的数据
 */
static uint32_t Flash_ReadWord(uint32_t addr)
{
    return *(volatile uint32_t *)addr;
}

/**
 * @brief 验证应用固件
 * @retval 1-有效 0-无效
 */
uint8_t VerifyAppFirmware(void)
{
    uint32_t app_stack_ptr = Flash_ReadWord(FLASH_APP_START_ADDR);
    uint32_t app_reset_vec = Flash_ReadWord(FLASH_APP_START_ADDR + 4);

    /* 检查栈指针和复位向量是否在有效范围内 */
    if ((app_stack_ptr >= 0x20000000) && (app_stack_ptr < 0x2000C000) &&
        (app_reset_vec >= FLASH_APP_START_ADDR) && (app_reset_vec < FLASH_APP_END_ADDR))
    {
        return 1;
    }
    return 0;
}

/**
 * @brief 检查升级标志
 * @retval 1-有升级标志 0-无升级标志
 */
uint8_t CheckUpgradeFlag(void)
{
    uint32_t flag = Flash_ReadWord(FLASH_FLAG_ADDR);
    return (flag == UPGRADE_FLAG_VALUE) ? 1 : 0;
}

/**
 * @brief 清除升级标志
 */
void ClearUpgradeFlag(void)
{
    uint32_t page_error = 0;
    FLASH_EraseInitTypeDef erase_init;

    HAL_FLASH_Unlock();

    /* 擦除包含升级标志的页（0x0803F800在最后一页） */
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.Banks = FLASH_BANK_1;
    erase_init.PageAddress = FLASH_FLAG_ADDR;
    erase_init.NbPages = 1;

    HAL_FLASHEx_Erase(&erase_init, &page_error);

    HAL_FLASH_Lock();
}

/**
 * @brief 跳转到应用固件
 * @param app_addr 应用固件起始地址
 */
void JumpToApp(uint32_t app_addr)
{
    typedef void (*pFunction)(void);
    pFunction jump_to_app;
    uint32_t jump_addr;

    /* 禁用所有中断 */
    __disable_irq();

    /* 关闭所有外设中断 */
    HAL_CAN_DeInit(&hcan);
    HAL_UART_DeInit(&huart1);

    /* 清除所有挂起的中断 */
    for (int i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    /* 重置SysTick */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    /* 设置向量表偏移到应用固件起始地址 */
    SCB->VTOR = app_addr;

    /* 获取应用固件的复位向量地址 */
    jump_addr = *(volatile uint32_t *)(app_addr + 4);
    jump_to_app = (pFunction)jump_addr;

    /* 设置主堆栈指针 */
    __set_MSP(*(volatile uint32_t *)app_addr);

    /* 清除所有挂起的中断标志 */
    __DSB();
    __ISB();

    /* 跳转到应用固件 */
    jump_to_app();
}
