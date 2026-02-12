/**
 * @file test_crc.c
 * @brief CRC算法库测试程序
 *
 * 对所有预定义算法进行校验测试，确保实现正确
 *
 * @author Claude Code
 * @date 2026-02-12
 * @license MIT
 */

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include "crc_api.h"

/* 测试结果统计 */
typedef struct {
    int total;      /* 总测试数 */
    int passed;     /* 通过数 */
    int failed;     /* 失败数 */
} test_stats_t;

/* 测试用例结构 */
typedef struct {
    crc_type_t type;        /* CRC算法类型 */
    const char *name;        /* 测试名称 */
    const char *data;        /* 测试数据 */
    uint64_t expected;       /* 期望结果 */
    uint8_t result_bytes;    /* 结果字节数 */
} test_case_t;

/* ============================================================================
 * 辅助函数
 * ============================================================================ */

/**
 * @brief 打印测试结果
 */
static void print_test_result(const char *name, uint64_t expected,
                               uint64_t actual, int passed)
{
    if (passed) {
        printf("  [PASS] %-20s: 0x%016llX\n", name, (unsigned long long)actual);
    } else {
        printf("  [FAIL] %-20s: Expected 0x%016llX, Got 0x%016llX\n",
               name, (unsigned long long)expected, (unsigned long long)actual);
    }
}

/**
 * @brief 运行单个测试用例
 */
static int run_test_case(const test_case_t *tc, test_stats_t *stats)
{
    uint64_t result = crc_compute(tc->type,
                                   (const uint8_t *)tc->data,
                                   strlen(tc->data));

    /* 根据结果字节数截取期望值 */
    uint64_t expected_mask = (tc->result_bytes == 8) ? 0xFFULL :
                              (tc->result_bytes == 16) ? 0xFFFFULL :
                              (tc->result_bytes == 24) ? 0xFFFFFFULL :
                              (tc->result_bytes == 32) ? 0xFFFFFFFFULL :
                              UINT64_MAX;
    uint64_t expected = tc->expected & expected_mask;
    uint64_t actual = result & expected_mask;

    int passed = (actual == expected);
    stats->total++;
    if (passed) {
        stats->passed++;
    } else {
        stats->failed++;
    }

    print_test_result(tc->name, expected, actual, passed);
    return passed;
}

/* ============================================================================
 * 测试套件
 * ============================================================================ */

/**
 * @brief 标准测试向量测试
 *
 * 使用字符串 "123456789" 作为标准测试数据
 */
static void test_standard_vectors(test_stats_t *stats)
{
    printf("\n========== 标准测试向量 (\"123456789\") ==========\n");

    const test_case_t tests[] = {
        /* ========== 8位CRC ========== */
        { CRC_8,        "CRC-8",        "123456789", 0xF4,       1 },
        { CRC_8_DARC,   "CRC-8-DARC",   "123456789", 0x15,       1 },
        { CRC_8_I_CODE, "CRC-8-I-CODE", "123456789", 0x7E,       1 },
        { CRC_8_ITU,    "CRC-8-ITU",    "123456789", 0xA1,       1 },
        { CRC_8_MAXIM,  "CRC-8-MAXIM",  "123456789", 0xA1,       1 },
        { CRC_8_ROHC,   "CRC-8-ROHC",   "123456789", 0xD0,       1 },
        { CRC_8_WCDMA,  "CRC-8-WCDMA",  "123456789", 0x25,       1 },

        /* ========== 16位CRC ========== */
        { CRC_16,         "CRC-16",         "123456789", 0xBB3D,     2 },
        { CRC_16_BYPASS,  "CRC-16-BYPASS",  "123456789", 0xFEE8,     2 },
        { CRC_16_DDS_110, "CRC-16-DDS-110", "123456789", 0x9ECF,     2 },
        { CRC_16_DECT,    "CRC-16-DECT",    "123456789", 0x007E,     2 },
        { CRC_16_DNP,     "CRC-16-DNP",     "123456789", 0xEA82,     2 },
        { CRC_16_EN_13757,"CRC-16-EN-13757","123456789", 0xC2B7,     2 },
        { CRC_16_GENIBUS, "CRC-16-GENIBUS", "123456789", 0xD64E,     2 },
        { CRC_16_MAXIM,   "CRC-16-MAXIM",   "123456789", 0x44C2,     2 },
        { CRC_16_MCRF4XX, "CRC-16-MCRF4XX", "123456789", 0x6F91,     2 },
        { CRC_16_RIELLO,  "CRC-16-RIELLO",  "123456789", 0x63D0,     2 },
        { CRC_16_T10_DIF, "CRC-16-T10-DIF", "123456789", 0xD0DB,     2 },
        { CRC_16_TELEDISK,"CRC-16-TELEDISK","123456789", 0x0FB3,     2 },
        { CRC_16_USB,     "CRC-16-USB",     "123456789", 0xB4C8,     2 },
        { CRC_X25,        "X-25",           "123456789", 0x906E,     2 },
        { CRC_XMODEM,     "XMODEM",         "123456789", 0x31C3,     2 },
        { CRC_MODBUS,     "MODBUS",         "123456789", 0x4B37,     2 },
        { CRC_CCITT_FALSE,"CRC-CCITT-FALSE","123456789", 0x29B1,     2 },
        { CRC_AUG_CCITT,  "CRC-AUG-CCITT",  "123456789", 0xE5CC,     2 },
        { CRC_KERMIT,     "KERMIT",         "123456789", 0x2189,     2 },

        /* ========== 24位CRC ========== */
        { CRC_24,         "CRC-24",         "123456789", 0x21CF02,   3 },
        { CRC_24_FLEXRAY_A,"CRC-24-FLEXRAY-A","123456789", 0x7979BD,   3 },
        { CRC_24_FLEXRAY_B,"CRC-24-FLEXRAY-B","123456789", 0x1F23B8,   3 },

        /* ========== 32位CRC ========== */
        { CRC_32,       "CRC-32",       "123456789", 0xCBF43926, 4 },
        { CRC_32_BZIP2, "CRC-32-BZIP2", "123456789", 0xFC891918, 4 },
        { CRC_32C,      "CRC-32C",      "123456789", 0xE3069283, 4 },
        { CRC_32D,      "CRC-32D",      "123456789", 0x87315576, 4 },
        { CRC_32_MPEG,  "CRC-32-MPEG",  "123456789", 0x0376E6E7, 4 },
        { CRC_POSIX,    "CRC-POSIX",    "123456789", 0x765E7680, 4 },
        { CRC_32Q,      "CRC-32Q",      "123456789", 0x3010BF7F, 4 },
        { CRC_JAMCRC,   "JAMCRC",       "123456789", 0x340BC6D9, 4 },
        { CRC_XFER,     "CRC-XFER",     "123456789", 0xBD0BE338, 4 },

        /* ========== 64位CRC ========== */
        { CRC_64,       "CRC-64",       "123456789", 0x46A5A9388A5BEFFE, 8 },
        { CRC_64_WE,    "CRC-64-WE",    "123456789", 0x62EC59E3F1A4F00A, 8 },
        { CRC_64_JONES, "CRC-64-JONES", "123456789", 0xCAA717168609F281, 8 },
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        run_test_case(&tests[i], stats);
    }
}

/**
 * @brief 边界测试
 */
static void test_edge_cases(test_stats_t *stats)
{
    (void)stats;  /* 避免未使用参数警告 */

    printf("\n========== 边界测试 ==========\n");

    /* 空数据测试 */
    uint64_t crc = crc_compute(CRC_32, (const uint8_t *)"", 0);
    printf("  [INFO] CRC-32 empty string: 0x%08llX\n", (unsigned long long)crc);

    /* 单字节测试 */
    uint8_t single_byte = 0xFF;
    crc = crc_compute(CRC_8, &single_byte, 1);
    printf("  [INFO] CRC-8 of 0xFF: 0x%02llX\n", (unsigned long long)crc);

    /* 零数据测试 */
    uint8_t zeros[16] = {0};
    crc = crc_compute(CRC_MODBUS, zeros, sizeof(zeros));
    printf("  [INFO] CRC-16-MODBUS of 16 zeros: 0x%04llX\n", (unsigned long long)crc);
}

/**
 * @brief 位反转函数测试
 *
 * 测试 bit_reverse8/16/32/64 函数的正确性
 */
static void test_bit_reverse(test_stats_t *stats)
{
    printf("\n========== 位反转测试 ==========\n");

    /* 8位位反转测试用例 */
    struct {
        uint8_t input;
        uint8_t expected;
        const char *desc;
    } test8[] = {
        { 0x00, 0x00, "全0" },
        { 0xFF, 0xFF, "全1" },
        { 0x01, 0x80, "0x01 -> 0x80" },
        { 0x80, 0x01, "0x80 -> 0x01" },
        { 0xD0, 0x0B, "0xD0 -> 0x0B" },
        { 0x0B, 0xD0, "0x0B -> 0xD0" },
        { 0x55, 0xAA, "0x55 -> 0xAA" },
        { 0xAA, 0x55, "0xAA -> 0x55" },
        { 0x12, 0x48, "0x12 -> 0x48" },
        { 0xC7, 0xE3, "0xC7 -> 0xE3" },
    };

    for (size_t i = 0; i < sizeof(test8) / sizeof(test8[0]); i++) {
        uint8_t result = bit_reverse8(test8[i].input);
        int passed = (result == test8[i].expected);
        stats->total++;
        if (passed) {
            stats->passed++;
        } else {
            stats->failed++;
        }
        if (passed) {
            printf("  [PASS] bit_reverse8(0x%02X) = 0x%02X (%s)\n",
                   test8[i].input, result, test8[i].desc);
        } else {
            printf("  [FAIL] bit_reverse8(0x%02X): Expected 0x%02X, Got 0x%02X (%s)\n",
                   test8[i].input, test8[i].expected, result, test8[i].desc);
        }
    }

    /* 16位位反转测试用例 */
    struct {
        uint16_t input;
        uint16_t expected;
        const char *desc;
    } test16[] = {
        { 0x0000, 0x0000, "全0" },
        { 0xFFFF, 0xFFFF, "全1" },
        { 0x0001, 0x8000, "0x0001 -> 0x8000" },
        { 0x8000, 0x0001, "0x8000 -> 0x0001" },
        { 0x1234, 0x2C48, "0x1234 -> 0x2C48" },
        { 0xABCD, 0xB3D5, "0xABCD -> 0xB3D5" },
        { 0x5555, 0xAAAA, "0x5555 -> 0xAAAA" },
        { 0xAAAA, 0x5555, "0xAAAA -> 0x5555" },
    };

    for (size_t i = 0; i < sizeof(test16) / sizeof(test16[0]); i++) {
        uint16_t result = bit_reverse16(test16[i].input);
        int passed = (result == test16[i].expected);
        stats->total++;
        if (passed) {
            stats->passed++;
        } else {
            stats->failed++;
        }
        if (passed) {
            printf("  [PASS] bit_reverse16(0x%04X) = 0x%04X (%s)\n",
                   test16[i].input, result, test16[i].desc);
        } else {
            printf("  [FAIL] bit_reverse16(0x%04X): Expected 0x%04X, Got 0x%04X (%s)\n",
                   test16[i].input, test16[i].expected, result, test16[i].desc);
        }
    }

    /* 32位位反转测试用例 */
    struct {
        uint32_t input;
        uint32_t expected;
        const char *desc;
    } test32[] = {
        { 0x00000000, 0x00000000, "全0" },
        { 0xFFFFFFFF, 0xFFFFFFFF, "全1" },
        { 0x00000001, 0x80000000, "0x00000001 -> 0x80000000" },
        { 0x80000000, 0x00000001, "0x80000000 -> 0x00000001" },
        { 0x12345678, 0x1E6A2C48, "0x12345678 -> 0x1E6A2C48" },
        { 0xABCDEF00, 0x00F7B3D5, "0xABCDEF00 -> 0x00F7B3D5" },
        { 0x55555555, 0xAAAAAAAA, "0x55555555 -> 0xAAAAAAAA" },
        { 0xAAAAAAAA, 0x55555555, "0xAAAAAAAA -> 0x55555555" },
    };

    for (size_t i = 0; i < sizeof(test32) / sizeof(test32[0]); i++) {
        uint32_t result = bit_reverse32(test32[i].input);
        int passed = (result == test32[i].expected);
        stats->total++;
        if (passed) {
            stats->passed++;
        } else {
            stats->failed++;
        }
        if (passed) {
            printf("  [PASS] bit_reverse32(0x%08X) = 0x%08X (%s)\n",
                   test32[i].input, result, test32[i].desc);
        } else {
            printf("  [FAIL] bit_reverse32(0x%08X): Expected 0x%08X, Got 0x%08X (%s)\n",
                   test32[i].input, test32[i].expected, result, test32[i].desc);
        }
    }

    /* 64位位反转测试用例 */
    struct {
        uint64_t input;
        uint64_t expected;
        const char *desc;
    } test64[] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, "全0" },
        { 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, "全1" },
        { 0x0000000000000001ULL, 0x8000000000000000ULL, "LSB -> MSB" },
        { 0x8000000000000000ULL, 0x0000000000000001ULL, "MSB -> LSB" },
        { 0x123456789ABCDEF0ULL, 0x0F7B3D591E6A2C48ULL, "0x123456789ABCDEF0" },
        { 0x5555555555555555ULL, 0xAAAAAAAAAAAAAAAAULL, "0x5555... -> 0xAAAA..." },
        { 0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL, "0xAAAA... -> 0x5555..." },
    };

    for (size_t i = 0; i < sizeof(test64) / sizeof(test64[0]); i++) {
        uint64_t result = bit_reverse64(test64[i].input);
        int passed = (result == test64[i].expected);
        stats->total++;
        if (passed) {
            stats->passed++;
        } else {
            stats->failed++;
        }
        if (passed) {
            printf("  [PASS] bit_reverse64(0x%016llX) = 0x%016llX (%s)\n",
                   (unsigned long long)test64[i].input,
                   (unsigned long long)result, test64[i].desc);
        } else {
            printf("  [FAIL] bit_reverse64(0x%016llX): Expected 0x%016llX, Got 0x%016llX (%s)\n",
                   (unsigned long long)test64[i].input,
                   (unsigned long long)test64[i].expected,
                   (unsigned long long)result, test64[i].desc);
        }
    }

    /* 通用 bit_reverse 函数测试 */
    struct {
        uint64_t input;
        uint8_t bits;
        uint64_t expected;
        const char *desc;
    } test_general[] = {
        { 0b11010000, 8, 0b00001011, "11010000(8bit) -> 00001011" },
        { 0b10000000, 8, 0b00000001, "10000000(8bit) -> 00000001" },
        { 0xAB, 8, 0xD5, "0xAB(8bit) -> 0xD5" },
        { 0x1234, 16, 0x2C48, "0x1234(16bit) -> 0x2C48" },
    };

    for (size_t i = 0; i < sizeof(test_general) / sizeof(test_general[0]); i++) {
        uint64_t result = bit_reverse(test_general[i].input, test_general[i].bits);
        int passed = (result == test_general[i].expected);
        stats->total++;
        if (passed) {
            stats->passed++;
        } else {
            stats->failed++;
        }
        if (passed) {
            printf("  [PASS] bit_reverse(0x%llX, %d) = 0x%llX (%s)\n",
                   (unsigned long long)test_general[i].input,
                   test_general[i].bits,
                   (unsigned long long)result, test_general[i].desc);
        } else {
            printf("  [FAIL] bit_reverse(0x%llX, %d): Expected 0x%llX, Got 0x%llX (%s)\n",
                   (unsigned long long)test_general[i].input,
                   test_general[i].bits,
                   (unsigned long long)test_general[i].expected,
                   (unsigned long long)result, test_general[i].desc);
        }
    }
}

/**
 * @brief 流式处理测试
 */
static void test_streaming(test_stats_t *stats)
{
    printf("\n========== 流式处理测试 ==========\n");

    const char *data = "The quick brown fox jumps over the lazy dog";
    size_t total_len = strlen(data);

    /* 一次性计算 */
    uint64_t crc_oneshot = crc_compute(CRC_32, (const uint8_t *)data, total_len);

    /* 分段计算 */
    crc_ctx_t ctx;
    crc_init(&ctx, CRC_32);

    size_t chunk_size = 10;
    for (size_t i = 0; i < total_len; i += chunk_size) {
        size_t len = (i + chunk_size < total_len) ? chunk_size : (total_len - i);
        crc_update_ctx(&ctx, (const uint8_t *)(data + i), len);
    }
    uint64_t crc_stream = crc_finalize_ctx(&ctx);

    int passed = (crc_oneshot == crc_stream);
    stats->total++;
    if (passed) {
        stats->passed++;
    } else {
        stats->failed++;
    }

    print_test_result("Streaming CRC-32", crc_oneshot, crc_stream, passed);
}

/**
 * @brief 实用示例演示
 */
static void demo_usage_examples(void)
{
    printf("\n========== 实用示例 ==========\n");

    /* 示例 1: Modbus CRC 计算 */
    printf("\n[示例 1] Modbus 协议消息 CRC:\n");
    uint8_t modbus_msg[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    uint64_t crc = crc_compute(CRC_MODBUS, modbus_msg, sizeof(modbus_msg));
    printf("  消息: %02X %02X %02X %02X %02X %02X\n",
           modbus_msg[0], modbus_msg[1], modbus_msg[2],
           modbus_msg[3], modbus_msg[4], modbus_msg[5]);
    printf("  CRC-16 Modbus: 0x%04llX\n", (unsigned long long)crc);

    /* 示例 2: ZIP 文件 CRC-32 */
    printf("\n[示例 2] ZIP 文件内容 CRC-32:\n");
    const char *file_content = "Hello, World!";
    crc = crc_compute(CRC_32, (const uint8_t *)file_content, strlen(file_content));
    printf("  内容: \"%s\"\n", file_content);
    printf("  CRC-32: 0x%08llX\n", (unsigned long long)crc);

    /* 示例 3: 自定义 CRC 配置 */
    printf("\n[示例 3] 自定义 CRC 配置:\n");
    crc_config_t custom_config = {
        .poly = 0x1021,
        .init_crc = 0xFFFF,
        .xor_out = 0x0000,
        .width_bits = 16,
        .reverse = 1,
        .refin = 1,
        .refout = 0
    };
    const char *test_data = "Custom CRC";
    crc = crc_calc((const uint8_t *)test_data, strlen(test_data), &custom_config);
    printf("  多项式: 0x%04llX, 初始值: 0x%04llX\n",
           (unsigned long long)custom_config.poly, (unsigned long long)custom_config.init_crc);
    printf("  数据: \"%s\"\n", test_data);
    printf("  自定义 CRC-16: 0x%04llX\n", (unsigned long long)crc);
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(void)
{
    test_stats_t stats = {0, 0, 0};

    printf("\n");
    printf("╔═══════════════════════════════════════════════════╗\n");
    printf("║         CRC 算法库测试程序 (不使用查表法)         ║\n");
    printf("╚═══════════════════════════════════════════════════╝\n");

    /* 运行所有测试 */
    test_standard_vectors(&stats);
    test_bit_reverse(&stats);
    test_edge_cases(&stats);
    test_streaming(&stats);

    /* 显示示例 */
    demo_usage_examples();

    /* 打印测试结果摘要 */
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║                    测试结果摘要                  ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  总测试数: %3d                                   ║\n", stats.total);
    printf("║  通过:     %3d                                   ║\n", stats.passed);
    printf("║  失败:     %3d                                   ║\n", stats.failed);
    printf("║  成功率:  %3.1f%%                                 ║\n",
           stats.total > 0 ? (100.0 * stats.passed / stats.total) : 0.0);
    printf("╚══════════════════════════════════════════════════╝\n");
    printf("\n");

    return (stats.failed == 0) ? 0 : 1;
}
