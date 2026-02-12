/**
 * @file crc_api.c
 * @brief CRC高级API接口实现
 *
 * @author Claude Code
 * @date 2026-02-12
 * @license MIT
 */

#include "crc_api.h"
#include <string.h>

/* ============================================================================
 * 预定义CRC算法配置表
 *
 * 格式: { 多项式, 初始值, 反向算法, XOR输出, 输出反转, 校验值 }
 *
 * 注意：多项式表示已经去掉了最高位（隐含的1）
 * 例如：标准CRC-16的多项式是 x^16 + x^15 + x^2 + 1 = 0x18005
 *      这里存储为 0x8005（去掉最高位后）
 *
 * 参数说明：
 * - reverse: 同时控制算法类型和输入反转（与crcmod的rev参数兼容）
 * - refout: 控制最终输出是否位反转
 * ============================================================================ */

/**
 * @brief CRC算法配置表
 *
 * 基于 crcmod.predefined 的定义
 * 校验值是对字符串 "123456789" 计算的CRC结果
 */
typedef struct {
    crc_type_t type;      /**< 算法类型 */
    const char *name;     /**< 算法名称 */
    crc_config_t config;  /**< 配置参数 */
    uint64_t check;       /**< 校验值 (针对 "123456789") */
} crc_entry_t;

/* 配置表辅助宏 - 简化配置初始化 */
/* reverse参数同时控制算法类型和输入反转（与crcmod兼容）*/
#define CFG8(p, i, r, xo, ro)  { .poly = p, .init_crc = i, .width_bits = 8,  .reverse = r, .xor_out = xo, .refin = r, .refout = ro }
#define CFG16(p, i, r, xo, ro) { .poly = p, .init_crc = i, .width_bits = 16, .reverse = r, .xor_out = xo, .refin = r, .refout = ro }
#define CFG24(p, i, r, xo, ro) { .poly = p, .init_crc = i, .width_bits = 24, .reverse = r, .xor_out = xo, .refin = r, .refout = ro }
#define CFG32(p, i, r, xo, ro) { .poly = p, .init_crc = i, .width_bits = 32, .reverse = r, .xor_out = xo, .refin = r, .refout = ro }
#define CFG64(p, i, r, xo, ro) { .poly = p, .init_crc = i, .width_bits = 64, .reverse = r, .xor_out = xo, .refin = r, .refout = ro }

/* CRC算法配置表 */
static const crc_entry_t crc_table[] = {
    /* ========== 8位CRC ========== */
    /* 参数：多项式, 初始值, reverse, xor_out, refout */
    { CRC_8,        "CRC-8",        .config = CFG8(0x07,      0x00,   0, 0x00, 0),          .check = 0xF4 },
    { CRC_8_DARC,   "CRC-8-DARC",   .config = CFG8(0x39,      0x00,   1, 0x00, 0),          .check = 0x15 },
    { CRC_8_I_CODE, "CRC-8-I-CODE", .config = CFG8(0x1D,      0xFD,   0, 0x00, 0),          .check = 0x7E },
    { CRC_8_ITU,    "CRC-8-ITU",    .config = CFG8(0x07,      0x55,   0, 0x55, 0),          .check = 0xA1 },
    { CRC_8_MAXIM,  "CRC-8-MAXIM",  .config = CFG8(0x31,      0x00,   1, 0x00, 0),          .check = 0xA1 },
    { CRC_8_ROHC,   "CRC-8-ROHC",   .config = CFG8(0x07,      0xFF,   1, 0x00, 0),          .check = 0xD0 },
    { CRC_8_WCDMA,  "CRC-8-WCDMA",  .config = CFG8(0x9B,      0x00,   1, 0x00, 0),          .check = 0x25 },

    /* ========== 16位CRC ========== */
    { CRC_16,         "CRC-16",         .config = CFG16(0x8005,   0x0000, 1, 0x0000, 0),   .check = 0xBB3D },
    { CRC_16_BYPASS,  "CRC-16-BYPASS",  .config = CFG16(0x8005,   0x0000, 0, 0x0000, 0),   .check = 0xFEE8 },
    { CRC_16_DDS_110, "CRC-16-DDS-110", .config = CFG16(0x8005,   0x800D, 0, 0x0000, 0),   .check = 0x9ECF },
    { CRC_16_DECT,    "CRC-16-DECT",    .config = CFG16(0x0589,   0x0001, 0, 0x0001, 0),   .check = 0x007E },
    { CRC_16_DNP,     "CRC-16-DNP",     .config = CFG16(0x3D65,   0xFFFF, 1, 0xFFFF, 0),   .check = 0xEA82 },
    { CRC_16_EN_13757,"CRC-16-EN-13757",.config = CFG16(0x3D65,   0xFFFF, 0, 0xFFFF, 0),   .check = 0xC2B7 },
    { CRC_16_GENIBUS, "CRC-16-GENIBUS", .config = CFG16(0x1021,   0x0000, 0, 0xFFFF, 0),   .check = 0xD64E },
    { CRC_16_MAXIM,   "CRC-16-MAXIM",   .config = CFG16(0x8005,   0xFFFF, 1, 0xFFFF, 0),   .check = 0x44C2 },
    { CRC_16_MCRF4XX, "CRC-16-MCRF4XX", .config = CFG16(0x1021,   0xFFFF, 1, 0x0000, 0),   .check = 0x6F91 },
    { CRC_16_RIELLO,  "CRC-16-RIELLO",  .config = CFG16(0x1021,   0x554D, 1, 0x0000, 0),   .check = 0x63D0 },
    { CRC_16_T10_DIF, "CRC-16-T10-DIF", .config = CFG16(0x8BB7,   0x0000, 0, 0x0000, 0),   .check = 0xD0DB },
    { CRC_16_TELEDISK,"CRC-16-TELEDISK",.config = CFG16(0xA097,   0x0000, 0, 0x0000, 0),   .check = 0x0FB3 },
    { CRC_16_USB,     "CRC-16-USB",     .config = CFG16(0x8005,   0x0000, 1, 0xFFFF, 0),   .check = 0xB4C8 },
    { CRC_X25,        "X-25",           .config = CFG16(0x1021,   0x0000, 1, 0xFFFF, 0),   .check = 0x906E },
    { CRC_XMODEM,     "XMODEM",         .config = CFG16(0x1021,   0x0000, 0, 0x0000, 0),   .check = 0x31C3 },
    { CRC_MODBUS,     "MODBUS",         .config = CFG16(0x8005,   0xFFFF, 1, 0x0000, 0),   .check = 0x4B37 },
    { CRC_CCITT_FALSE,"CRC-CCITT-FALSE",.config = CFG16(0x1021,   0xFFFF, 0, 0x0000, 0),   .check = 0x29B1 },
    { CRC_AUG_CCITT,  "CRC-AUG-CCITT",  .config = CFG16(0x1021,   0x1D0F, 0, 0x0000, 0),   .check = 0xE5CC },
    { CRC_KERMIT,     "KERMIT",         .config = CFG16(0x1021,   0x0000, 1, 0x0000, 0),   .check = 0x2189 },

    /* ========== 24位CRC ========== */
    { CRC_24,         "CRC-24",         .config = CFG24(0x864CFB, 0xB704CE, 0, 0x000000, 0), .check = 0x21CF02 },
    { CRC_24_FLEXRAY_A,"CRC-24-FLEXRAY-A",.config=CFG24(0x5D6DCB, 0xFEDCBA, 0, 0x000000, 0),.check = 0x7979BD },
    { CRC_24_FLEXRAY_B,"CRC-24-FLEXRAY-B",.config=CFG24(0x5D6DCB, 0xABCDEF, 0, 0x000000, 0),.check = 0x1F23B8 },

    /* ========== 32位CRC ========== */
    { CRC_32,       "CRC-32",       .config = CFG32(0x04C11DB7, 0x00000000, 1, 0xFFFFFFFF, 0), .check = 0xCBF43926 },
    { CRC_32_BZIP2, "CRC-32-BZIP2", .config = CFG32(0x04C11DB7, 0x00000000, 0, 0xFFFFFFFF, 0), .check = 0xFC891918 },
    { CRC_32C,      "CRC-32C",      .config = CFG32(0x1EDC6F41, 0x00000000, 1, 0xFFFFFFFF, 0), .check = 0xE3069283 },
    { CRC_32D,      "CRC-32D",      .config = CFG32(0xA833982B, 0x00000000, 1, 0xFFFFFFFF, 0), .check = 0x87315576 },
    { CRC_32_MPEG,  "CRC-32-MPEG",  .config = CFG32(0x04C11DB7, 0xFFFFFFFF, 0, 0x00000000, 0), .check = 0x0376E6E7 },
    { CRC_POSIX,    "CRC-POSIX",    .config = CFG32(0x04C11DB7, 0xFFFFFFFF, 0, 0xFFFFFFFF, 0), .check = 0x765E7680 },
    { CRC_32Q,      "CRC-32Q",      .config = CFG32(0x814141AB, 0x00000000, 0, 0x00000000, 0), .check = 0x3010BF7F },
    { CRC_JAMCRC,   "JAMCRC",       .config = CFG32(0x04C11DB7, 0xFFFFFFFF, 1, 0x00000000, 0), .check = 0x340BC6D9 },
    { CRC_XFER,     "CRC-XFER",     .config = CFG32(0x000000AF, 0x00000000, 0, 0x00000000, 0), .check = 0xBD0BE338 },

    /* ========== 64位CRC ========== */
    { CRC_64,       "CRC-64",       .config = CFG64(0x000000000000001B, 0x0000000000000000, 1, 0x0000000000000000, 0), .check = 0x46A5A9388A5BEFFE },
    { CRC_64_WE,    "CRC-64-WE",    .config = CFG64(0x42F0E1EBA9EA3693, 0x0000000000000000, 0, 0xFFFFFFFFFFFFFFFF, 0), .check = 0x62EC59E3F1A4F00A },
    { CRC_64_JONES, "CRC-64-JONES", .config = CFG64(0xAD93D23594C935A9, 0xFFFFFFFFFFFFFFFF, 1, 0x0000000000000000, 0), .check = 0xCAA717168609F281 },
};

#define CRC_TABLE_SIZE (sizeof(crc_table) / sizeof(crc_table[0]))

/* ============================================================================
 * 内部查找函数
 * ============================================================================ */

/**
 * @brief 在配置表中查找指定类型的配置
 *
 * @param type CRC类型
 * @return const crc_entry_t* 配置表项指针，未找到返回NULL
 */
static const crc_entry_t *_find_entry(crc_type_t type)
{
    for (size_t i = 0; i < CRC_TABLE_SIZE; i++) {
        if (crc_table[i].type == type) {
            return &crc_table[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * 公共接口实现
 * ============================================================================ */

int crc_get_config(crc_type_t type, crc_config_t *config)
{
    const crc_entry_t *entry = _find_entry(type);
    if (entry == NULL || config == NULL) {
        return -1;
    }
    *config = entry->config;
    return 0;
}

int crc_get_check_value(crc_type_t type, uint64_t *check_value)
{
    const crc_entry_t *entry = _find_entry(type);
    if (entry == NULL || check_value == NULL) {
        return -1;
    }
    *check_value = entry->check;
    return 0;
}

const char *crc_get_name(crc_type_t type)
{
    const crc_entry_t *entry = _find_entry(type);
    if (entry == NULL) {
        return "UNKNOWN";
    }
    return entry->name;
}

uint64_t crc_compute(crc_type_t type, const uint8_t *data, size_t len)
{
    const crc_entry_t *entry = _find_entry(type);
    if (entry == NULL) {
        return 0;
    }
    return crc_calc(data, len, &entry->config);
}

int crc_init(crc_ctx_t *ctx, crc_type_t type)
{
    if (ctx == NULL) {
        return -1;
    }

    const crc_entry_t *entry = _find_entry(type);
    if (entry == NULL) {
        return -1;
    }

    ctx->config = entry->config;
    /* 应用"首尾异或"逻辑以匹配 crc_calc 的行为 */
    if (ctx->config.xor_out != 0) {
        ctx->crc = ctx->config.xor_out ^ ctx->config.init_crc;
    } else {
        ctx->crc = ctx->config.init_crc;
    }
    return 0;
}

int crc_init_custom(crc_ctx_t *ctx, const crc_config_t *config)
{
    if (ctx == NULL || config == NULL) {
        return -1;
    }

    ctx->config = *config;
    /* 应用"首尾异或"逻辑以匹配 crc_calc 的行为 */
    if (ctx->config.xor_out != 0) {
        ctx->crc = ctx->config.xor_out ^ ctx->config.init_crc;
    } else {
        ctx->crc = ctx->config.init_crc;
    }
    return 0;
}

int crc_update_ctx(crc_ctx_t *ctx, const uint8_t *data, size_t len)
{
    if (ctx == NULL || data == NULL) {
        return -1;
    }

    ctx->crc = crc_update(data, len, ctx->crc, &ctx->config);
    return 0;
}

uint64_t crc_finalize_ctx(crc_ctx_t *ctx)
{
    if (ctx == NULL) {
        return 0;
    }

    return crc_finalize(ctx->crc, &ctx->config);
}

int crc_reset(crc_ctx_t *ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    /* 应用"首尾异或"逻辑以匹配 crc_calc 的行为 */
    if (ctx->config.xor_out != 0) {
        ctx->crc = ctx->config.xor_out ^ ctx->config.init_crc;
    } else {
        ctx->crc = ctx->config.init_crc;
    }
    return 0;
}
