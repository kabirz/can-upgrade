/**
 * @file crc.h
 * @brief CRC核心算法定义 - 不使用查表法的位运算实现
 *
 * 基于 crcmod (Python) 的算法实现，使用纯位运算计算CRC，
 * 不依赖查找表，适用于ROM/RAM受限的嵌入式系统。
 *
 * 核心算法原理：
 * 1. CRC是循环冗余校验码，通过多项式除法计算
 * 2. 数据被视为一个大二进制数，除以生成多项式
 * 3. 余数即为CRC值
 *
 * @author Claude Code
 * @date 2026-02-12
 * @license MIT
 */

#ifndef CRC_H
#define CRC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 常量定义
 * ============================================================================ */

/** @brief 最大支持的多项式位数 */
#define CRC_MAX_BITS 64

/* ============================================================================
 * 数据结构
 * ============================================================================ */

/**
 * @brief CRC算法配置结构体
 *
 * 包含计算CRC所需的所有参数
 */
typedef struct {
    uint64_t poly;          /**< 生成多项式（不含最高位） */
    uint64_t init_crc;      /**< 初始CRC值 */
    uint64_t xor_out;       /**< 最终异或输出值 */
    uint8_t width_bits;     /**< CRC宽度（8/16/24/32/64） */
    uint8_t reverse;        /**< 是否使用位反转算法 */
    uint8_t refin;          /**< 输入数据是否位反转 */
    uint8_t refout;         /**< 输出结果是否位反转 */
} crc_config_t;

/* ============================================================================
 * 位反转工具函数
 * ============================================================================ */

/**
 * @brief 位反转函数 - 反转n位数据
 *
 * @param x 输入值
 * @param n 需要反转的位数
 * @return uint64_t 反转后的值
 *
 * @example
 * @code
 * bit_reverse(0b11010000, 8) = 0b00001011 = 0x0B
 * @endcode
 */
static inline uint64_t bit_reverse(uint64_t x, uint8_t n)
{
    uint64_t y = 0;
    for (uint8_t i = 0; i < n; i++) {
        y = (y << 1) | (x & 1);
        x >>= 1;
    }
    return y;
}

/**
 * @brief 8位数据位反转
 *
 * @param x 输入字节
 * @return uint8_t 反转后的字节
 */
static inline uint8_t bit_reverse8(uint8_t x)
{
    x = ((x & 0xAA) >> 1) | ((x & 0x55) << 1);
    x = ((x & 0xCC) >> 2) | ((x & 0x33) << 2);
    x = ((x & 0xF0) >> 4) | ((x & 0x0F) << 4);
    return x;
}

/**
 * @brief 16位数据位反转
 *
 * @param x 输入字
 * @return uint16_t 反转后的字
 */
static inline uint16_t bit_reverse16(uint16_t x)
{
    x = ((x & 0xAAAA) >> 1) | ((x & 0x5555) << 1);
    x = ((x & 0xCCCC) >> 2) | ((x & 0x3333) << 2);
    x = ((x & 0xF0F0) >> 4) | ((x & 0x0F0F) << 4);
    x = ((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8);
    return x;
}

/**
 * @brief 32位数据位反转
 *
 * @param x 输入双字
 * @return uint32_t 反转后的双字
 */
static inline uint32_t bit_reverse32(uint32_t x)
{
    x = ((x & 0xAAAAAAAA) >> 1) | ((x & 0x55555555) << 1);
    x = ((x & 0xCCCCCCCC) >> 2) | ((x & 0x33333333) << 2);
    x = ((x & 0xF0F0F0F0) >> 4) | ((x & 0x0F0F0F0F) << 4);
    x = ((x & 0xFF00FF00) >> 8) | ((x & 0x00FF00FF) << 8);
    x = ((x & 0xFFFF0000) >> 16) | ((x & 0x0000FFFF) << 16);
    return x;
}

/**
 * @brief 64位数据位反转
 *
 * @param x 输入四字
 * @return uint64_t 反转后的四字
 */
static inline uint64_t bit_reverse64(uint64_t x)
{
    x = ((x & 0xAAAAAAAAAAAAAAAA) >> 1) | ((x & 0x5555555555555555) << 1);
    x = ((x & 0xCCCCCCCCCCCCCCCC) >> 2) | ((x & 0x3333333333333333) << 2);
    x = ((x & 0xF0F0F0F0F0F0F0F0) >> 4) | ((x & 0x0F0F0F0F0F0F0F0F) << 4);
    x = ((x & 0xFF00FF00FF00FF00) >> 8) | ((x & 0x00FF00FF00FF00FF) << 8);
    x = ((x & 0xFFFF0000FFFF0000) >> 16) | ((x & 0x0000FFFF0000FFFF) << 16);
    x = (x >> 32) | (x << 32);
    return x;
}

/* ============================================================================
 * 核心算法 - 单字节CRC计算（不使用查表法）
 * ============================================================================ */

/**
 * @brief 单字节CRC计算 - 前向算法
 *
 * 对单个字节进行CRC计算，使用前向（非位反转）算法。
 * 这是CRC计算的最底层操作，每次处理8位数据。
 *
 * @param crc 当前CRC值
 * @param data 输入数据字节
 * @param poly 生成多项式（已去掉最高位）
 * @param width_bits CRC宽度（位数）
 * @return uint64_t 更新后的CRC值
 *
 * @算法说明：
 * 1. 将数据字节对齐到CRC的高8位
 * 2. 对每个bit位置：
 *    - 检查最高位是否为1
 *    - 若为1，则左移并异或多项式（模拟除法）
 *    - 若为0，则仅左移
 * 3. 最后进行掩码处理，保留有效位数
 */
static inline uint64_t _byte_crc_forward(uint64_t crc, uint8_t data,
                                          uint64_t poly, uint8_t width_bits)
{
    /* 将数据字节对齐到CRC的高8位 */
    crc ^= ((uint64_t)data << (width_bits - 8));

    /* 最高位掩码 - 用于检查是否需要异或多项式 */
    const uint64_t msb_mask = 1ULL << (width_bits - 1);
    /* 结果掩码 - 保留有效位数 */
    const uint64_t result_mask = (width_bits == 64) ? UINT64_MAX : ((1ULL << width_bits) - 1);

    /* 处理8个bit */
    for (uint8_t i = 0; i < 8; i++) {
        if (crc & msb_mask) {
            crc = (crc << 1) ^ poly;
        } else {
            crc = crc << 1;
        }
    }

    return crc & result_mask;
}

/**
 * @brief 单字节CRC计算 - 反向算法（位反转）
 *
 * 对单个字节进行CRC计算，使用位反转算法。
 * 适用于需要LSB优先处理的场景。
 *
 * @param crc 当前CRC值
 * @param data 输入数据字节
 * @param poly 生成多项式（已位反转）
 * @param width_bits CRC宽度（位数）
 * @return uint64_t 更新后的CRC值
 *
 * @算法说明：
 * 1. 数据字节直接与CRC低8位异或
 * 2. 对每个bit位置：
 *    - 检查最低位是否为1
 *    - 若为1，则右移并异或多项式
 *    - 若为0，则仅右移
 * 3. 最后进行掩码处理
 */
static inline uint64_t _byte_crc_reverse(uint64_t crc, uint8_t data,
                                          uint64_t poly, uint8_t width_bits)
{
    /* 数据字节直接与CRC低8位异或 */
    crc ^= data;

    /* 结果掩码 */
    const uint64_t result_mask = (width_bits == 64) ? UINT64_MAX : ((1ULL << width_bits) - 1);

    /* 处理8个bit */
    for (uint8_t i = 0; i < 8; i++) {
        if (crc & 1) {
            crc = (crc >> 1) ^ poly;
        } else {
            crc = crc >> 1;
        }
    }

    return crc & result_mask;
}

/* ============================================================================
 * 完整CRC计算函数
 * ============================================================================ */

/**
 * @brief 计算数据块的CRC值
 *
 * @param data 输入数据缓冲区
 * @param len 数据长度
 * @param config CRC配置参数
 * @return uint64_t 计算得到的CRC值
 *
 * @note 返回值的高位可能包含未使用的位，调用者需要根据width_bits截取
 */
uint64_t crc_calc(const uint8_t *data, size_t len, const crc_config_t *config);

/**
 * @brief 计算数据块的CRC值（带初始CRC）
 *
 * @param data 输入数据缓冲区
 * @param len 数据长度
 * @param init_crc 初始CRC值
 * @param config CRC配置参数
 * @return uint64_t 计算得到的CRC值
 */
uint64_t crc_calc_with_init(const uint8_t *data, size_t len,
                             uint64_t init_crc, const crc_config_t *config);

/**
 * @brief 分段计算CRC值（用于流式数据处理）
 *
 * @param data 输入数据缓冲区
 * @param len 数据长度
 * @param crc 当前CRC值
 * @param config CRC配置参数
 * @return uint64_t 更新后的CRC值
 *
 * @note 首次调用时，crc应传入config->init_crc
 */
uint64_t crc_update(const uint8_t *data, size_t len,
                     uint64_t crc, const crc_config_t *config);

/**
 * @brief 完成CRC计算（应用最终异或和位反转）
 *
 * @param crc 当前CRC值
 * @param config CRC配置参数
 * @return uint64_t 最终CRC值
 */
uint64_t crc_finalize(uint64_t crc, const crc_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* CRC_H */
