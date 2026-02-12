/**
 * @file crc_api.h
 * @brief CRC高级API接口 - 预定义CRC算法
 *
 * 提供常见CRC标准的预定义配置和便捷函数
 * 参考 crcmod.predefined 实现
 *
 * @author Claude Code
 * @date 2026-02-12
 * @license MIT
 */

#ifndef CRC_API_H
#define CRC_API_H

#include "crc.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 预定义CRC算法枚举
 * ============================================================================ */

/**
 * @brief 支持的预定义CRC算法类型
 *
 * 涵盖了最常见的CRC标准，包括：
 * - CRC-8 系列
 * - CRC-16 系列（Modbus, CCITT, USB等）
 * - CRC-32 系列
 * - CRC-64 系列
 */
typedef enum {
    /* ========== 8位CRC ========== */
    CRC_8,              /**< 标准 CRC-8 */
    CRC_8_DARC,         /**< DARC CRC-8 */
    CRC_8_I_CODE,       /**< I-Code CRC-8 */
    CRC_8_ITU,          /**< ITU CRC-8 */
    CRC_8_MAXIM,        /**< Maxim CRC-8 (如 Dallas/Maxim iButton) */
    CRC_8_ROHC,         /**< ROHC CRC-8 */
    CRC_8_WCDMA,        /**< WCDMA CRC-8 */

    /* ========== 16位CRC ========== */
    CRC_16,             /**< 标准 CRC-16 */
    CRC_16_BYPASS,      /**< CRC-16 Bypass */
    CRC_16_DDS_110,     /**< DDS-110 CRC-16 */
    CRC_16_DECT,        /**< DECT CRC-16 */
    CRC_16_DNP,         /**< DNP CRC-16 */
    CRC_16_EN_13757,    /**< EN-13757 CRC-16 */
    CRC_16_GENIBUS,     /**< Genibus CRC-16 */
    CRC_16_MAXIM,       /**< Maxim CRC-16 */
    CRC_16_MCRF4XX,     /**< MCRF4XX CRC-16 */
    CRC_16_RIELLO,      /**< Riello CRC-16 */
    CRC_16_T10_DIF,     /**< T10-DIF CRC-16 */
    CRC_16_TELEDISK,    /**< Teledisk CRC-16 */
    CRC_16_USB,         /**< USB CRC-16 */

    /* CCITT变体 */
    CRC_CCITT_FALSE,    /**< False CCITT (多项式 0x1021, 初始值 0xFFFF) */
    CRC_AUG_CCITT,      /**< Augmented CCITT */
    CRC_KERMIT,         /**< Kermit协议 */
    CRC_XMODEM,         /**< XMODEM协议 */
    CRC_X25,            /**< X.25协议 */

    /* 工业协议 */
    CRC_MODBUS,         /**< Modbus协议 CRC-16 */

    /* ========== 24位CRC ========== */
    CRC_24,             /**< 标准 CRC-24 */
    CRC_24_FLEXRAY_A,   /**< FlexRay-A CRC-24 */
    CRC_24_FLEXRAY_B,   /**< FlexRay-B CRC-24 */

    /* ========== 32位CRC ========== */
    CRC_32,             /**< 标准 CRC-32 (如 ZIP, PNG) */
    CRC_32_BZIP2,       /**< BZIP2 CRC-32 */
    CRC_32C,            /**< Castagnoli CRC-32 (如 iSCSI) */
    CRC_32D,            /**< CRC-32D */
    CRC_32_MPEG,        /**< MPEG-2 CRC-32 */
    CRC_32Q,            /**< CRC-32Q */
    CRC_POSIX,          /**< POSIX CRC-32 */
    CRC_JAMCRC,         /**< JAMCRC */
    CRC_XFER,           /**< XFER CRC-32 */

    /* ========== 64位CRC ========== */
    CRC_64,             /**< 标准 CRC-64 (如 ECMA-182) */
    CRC_64_WE,          /**< CRC-64-WE */
    CRC_64_JONES,       /**< CRC-64-Jones */

    /* 用户自定义 */
    CRC_CUSTOM = 0xFF,   /**< 用户自定义CRC（使用自定义配置） */
} crc_type_t;

/* ============================================================================
 * 预定义CRC算法配置获取
 * ============================================================================ */

/**
 * @brief 获取预定义CRC算法的配置
 *
 * @param type CRC算法类型
 * @param config 输出配置结构体指针
 * @return 0 成功, -1 失败（未知类型）
 */
int crc_get_config(crc_type_t type, crc_config_t *config);

/**
 * @brief 获取CRC算法的校验值（用于测试）
 *
 * 返回 ASCII 字符串 "123456789" 的CRC值
 *
 * @param type CRC算法类型
 * @param check_value 输出校验值
 * @return 0 成功, -1 失败（未知类型）
 */
int crc_get_check_value(crc_type_t type, uint64_t *check_value);

/* ============================================================================
 * 便捷计算函数 - 一次性计算
 * ============================================================================ */

/**
 * @brief 使用预定义算法计算CRC
 *
 * @param type CRC算法类型
 * @param data 输入数据
 * @param len 数据长度
 * @return uint64_t CRC值
 */
uint64_t crc_compute(crc_type_t type, const uint8_t *data, size_t len);

/* ============================================================================
 * 上下文接口 - 流式处理
 * ============================================================================ */

/**
 * @brief CRC计算上下文
 *
 * 用于流式处理大数据时的状态保持
 */
typedef struct {
    crc_config_t config;    /**< CRC配置 */
    uint64_t crc;           /**< 当前CRC值 */
} crc_ctx_t;

/**
 * @brief 初始化CRC上下文
 *
 * @param ctx 上下文结构体指针
 * @param type CRC算法类型
 * @return 0 成功, -1 失败
 */
int crc_init(crc_ctx_t *ctx, crc_type_t type);

/**
 * @brief 使用自定义配置初始化CRC上下文
 *
 * @param ctx 上下文结构体指针
 * @param config CRC配置
 * @return 0 成功, -1 失败
 */
int crc_init_custom(crc_ctx_t *ctx, const crc_config_t *config);

/**
 * @brief 更新CRC计算（流式处理）
 *
 * @param ctx 上下文结构体指针
 * @param data 输入数据
 * @param len 数据长度
 * @return 0 成功, -1 失败
 */
int crc_update_ctx(crc_ctx_t *ctx, const uint8_t *data, size_t len);

/**
 * @brief 完成CRC计算并返回结果
 *
 * @param ctx 上下文结构体指针
 * @return uint64_t 最终CRC值
 */
uint64_t crc_finalize_ctx(crc_ctx_t *ctx);

/**
 * @brief 重置CRC上下文到初始状态
 *
 * @param ctx 上下文结构体指针
 * @return 0 成功, -1 失败
 */
int crc_reset(crc_ctx_t *ctx);

/* ============================================================================
 * CRC算法名称字符串（用于调试/日志）
 * ============================================================================ */

/**
 * @brief 获取CRC算法的名称
 *
 * @param type CRC算法类型
 * @return const char* 算法名称字符串，未知类型返回 "UNKNOWN"
 */
const char *crc_get_name(crc_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* CRC_API_H */
