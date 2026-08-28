/*
 * sha256.h — SHA-256 软件实现 (FIPS 180-4)
 *
 * ======================
 * 用途
 * ======================
 * ATSHA204A 所有 TempKey / MAC 的主机侧计算均依赖 SHA-256:
 *   - Nonce   TK:  55 字节 → 32 字节摘要
 *   - GenDig  TK:  96 字节 → 32 字节摘要
 *   - MAC 验证:    88 字节 → 32 字节摘要
 *   - Write  MAC:  96 字节 → 32 字节摘要
 *   - 多元化密钥:   64 字节 → 32 字节摘要
 *
 * 所有输入 ≤ 96 字节，单次 sha256() 调用即可完成，无需流式接口。
 *
 * ======================
 * 性能 (STM32F407 168MHz)
 * ======================
 * 单次 64 字节块 ≈ 0.3 ms
 * 单次 96 字节块 ≈ 0.5 ms (两个 512-bit 块)
 */

#ifndef __SHA256_H__
#define __SHA256_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 摘要长度 ──────────────────────────────────────────────────────────────── */
#define SHA256_DIGEST_SIZE  32

/* ── 上下文 (保留, 供将来流式扩展) ──────────────────────────────────────────── */
typedef struct {
    uint8_t  buf[64];       /* 数据块缓冲区        */
    uint32_t state[8];      /* 中间哈希状态 H0-H7  */
    uint64_t count;         /* 已处理的总 bit 数   */
} sha256_ctx_t;

/* ── 单次哈希 ──────────────────────────────────────────────────────────────── */
void sha256(const uint8_t *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* __SHA256_H__ */
