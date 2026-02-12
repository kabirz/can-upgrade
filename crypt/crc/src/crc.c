/**
 * @file crc.c
 * @brief CRC核心算法实现 - 不使用查表法的位运算实现
 *
 * @author Claude Code
 * @date 2026-02-12
 * @license MIT
 */

#include "crc.h"
#include <string.h>

/* ============================================================================
 * 私有辅助函数
 * ============================================================================ */

/**
 * @brief 获取CRC宽度的掩码值
 *
 * @param width_bits CRC宽度（位数）
 * @return uint64_t 掩码值
 */
static inline uint64_t _get_width_mask(uint8_t width_bits)
{
    if (width_bits == 64) {
        return UINT64_MAX;
    }
    return ((1ULL << width_bits) - 1);
}

/**
 * @brief 准备生成多项式
 *
 * 根据是否使用反转算法，对多项式进行预处理
 *
 * @param poly 原始多项式
 * @param reverse 是否使用反转算法
 * @param width_bits CRC宽度
 * @return uint64_t 处理后的多项式
 */
static uint64_t _prepare_poly(uint64_t poly, uint8_t reverse, uint8_t width_bits)
{
    const uint64_t mask = _get_width_mask(width_bits);
    poly = poly & mask;

    if (reverse) {
        /* 反转多项式 */
        poly = bit_reverse(poly, width_bits);
    }

    return poly;
}

/* ============================================================================
 * 公共接口实现
 * ============================================================================ */

/**
 * @brief 分段计算CRC值（用于流式数据处理）
 *
 * 这是CRC计算的核心函数，处理任意长度的数据。
 *
 * @param data 输入数据缓冲区
 * @param len 数据长度
 * @param crc 当前CRC值（首次调用时使用配置的初始值）
 * @param config CRC配置参数
 * @return uint64_t 更新后的CRC值
 *
 * @处理流程：
 * 1. 应用初始值异或（如果需要）
 * 2. 对每个数据字节：
 *    - 根据reverse参数选择算法类型
 *    - 反向算法不需要对输入数据进行额外位反转（已体现在多项式中）
 * 3. 返回更新后的CRC值
 *
 * @note 此函数不应用最终异或和输出反转，需在最后一次调用后使用crc_finalize()
 *
 * @设计说明：
 * 与crcmod兼容的实现：
 * - reverse=0 (前向): 使用_byte_crc_forward，数据对齐到高位后处理
 * - reverse=1 (反向): 使用_byte_crc_reverse，数据直接与低位异或
 * - 多项式在_prepare_poly中已根据reverse参数进行预处理
 * - refout: 控制最终输出是否位反转（在crc_finalize中处理）
 *
 * @重要：对于反向算法，输入数据不需要位反转！
 * 位反转是算法本身的特性，已经体现在表/多项式的处理中。
 * refin参数在crcmod中实际上是reverse的别名，不是独立的输入反转标志。
 */
uint64_t crc_update(const uint8_t *data, size_t len,
                     uint64_t crc, const crc_config_t *config)
{
    if (config == NULL || data == NULL) {
        return crc;
    }

    if (len == 0) {
        return crc;
    }

    /* 应用掩码确保CRC在有效范围内 */
    const uint64_t mask = _get_width_mask(config->width_bits);
    crc = crc & mask;

    /* 准备多项式（根据reverse参数决定是否位反转）*/
    uint64_t poly = _prepare_poly(config->poly, config->reverse, config->width_bits);

    /* 处理每个字节 */
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];

        /* 根据算法类型选择计算方式 */
        /* 注意：输入数据不需要位反转，反转已体现在多项式/算法中 */
        if (config->reverse) {
            crc = _byte_crc_reverse(crc, byte, poly, config->width_bits);
        } else {
            crc = _byte_crc_forward(crc, byte, poly, config->width_bits);
        }
    }

    return crc;
}

/**
 * @brief 完成CRC计算（应用最终异或和位反转）
 *
 * @param crc 当前CRC值
 * @param config CRC配置参数
 * @return uint64_t 最终CRC值
 */
uint64_t crc_finalize(uint64_t crc, const crc_config_t *config)
{
    if (config == NULL) {
        return crc;
    }

    /* 应用掩码 */
    const uint64_t mask = _get_width_mask(config->width_bits);
    crc = crc & mask;

    /* 最终异或 */
    crc ^= config->xor_out;

    /* 输出反转 */
    if (config->refout) {
        crc = bit_reverse(crc, config->width_bits);
    }

    /* 再次应用掩码确保结果正确 */
    crc = crc & mask;

    return crc;
}

/**
 * @brief 计算数据块的CRC值
 *
 * 一次性完成CRC计算的便捷函数
 *
 * @param data 输入数据缓冲区
 * @param len 数据长度
 * @param config CRC配置参数
 * @return uint64_t 计算得到的CRC值
 *
 * @设计说明：
 * 兼容crcmod的"首尾异或"技术：
 * 当 xorOut != 0 时，crcmod 使用以下处理：
 * 1. 初始 crc 先与 xorOut 异或
 * 2. 处理数据
 * 3. 结果再与 xorOut 异或
 *
 * 这样确保空字符串返回 xorOut 值。
 */
uint64_t crc_calc(const uint8_t *data, size_t len, const crc_config_t *config)
{
    if (config == NULL) {
        return 0;
    }

    uint64_t crc = config->init_crc;

    /* 应用 crcmod 的"首尾异或"技术（当 xorOut != 0 时）*/
    /* 这确保空字符串返回 xorOut 值 */
    if (config->xor_out != 0) {
        crc = config->xor_out ^ crc;
    }

    crc = crc_update(data, len, crc, config);
    crc = crc_finalize(crc, config);

    return crc;
}

/**
 * @brief 计算数据块的CRC值（带初始CRC）
 *
 * 使用指定的初始CRC值进行计算
 *
 * @param data 输入数据缓冲区
 * @param len 数据长度
 * @param init_crc 初始CRC值
 * @param config CRC配置参数
 * @return uint64_t 计算得到的CRC值
 *
 * @注意：此函数直接使用传入的 init_crc，不应用"首尾异或"技术
 * 如果需要与 crcmod 完全兼容的行为，请使用 crc_calc 函数。
 */
uint64_t crc_calc_with_init(const uint8_t *data, size_t len,
                             uint64_t init_crc, const crc_config_t *config)
{
    if (config == NULL) {
        return 0;
    }

    uint64_t crc = init_crc;
    crc = crc_update(data, len, crc, config);
    crc = crc_finalize(crc, config);

    return crc;
}
