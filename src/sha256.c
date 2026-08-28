/*
 * sha256.c — SHA-256 软件实现 (FIPS 180-4)
 *
 * 算法流程 (单次调用):
 *   1. 将输入数据 memcpy 到内部缓冲区
 *   2. 执行 SHA-256 padding (追加 0x80 + zeros + 64-bit 长度)
 *   3. 逐 512-bit 块压缩, 每块 64 轮
 *   4. 输出 256-bit (32 字节) 摘要
 */

#include "sha256.h"
#include <rtthread.h>
#include <string.h>

/* ── SHA-256 常量 ───────────────────────────────────────────────────────────── */
/* 初始哈希值 H0-H7 (FIPS 180-4 §5.3.3) */
static const uint32_t SHA256_INIT[8] = {
    0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
    0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
};

/* 64 轮常数 K0-K63 (FIPS 180-4 §4.2.2) */
static const uint32_t SHA256_K[64] = {
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
    0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
    0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
    0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
    0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
    0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
    0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
    0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
    0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
    0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
    0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
    0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
    0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
    0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

/* ── 内联辅助 ───────────────────────────────────────────────────────────────── */

/* 循环右移 */
static inline uint32_t rotr32(uint32_t x, uint8_t n)
{
    return (x >> n) | (x << (32 - n));
}

/* 消息调度函数 (FIPS 180-4 §4.1.2) */
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BSIG0(x) (rotr32(x,  2) ^ rotr32(x, 13) ^ rotr32(x, 22))
#define BSIG1(x) (rotr32(x,  6) ^ rotr32(x, 11) ^ rotr32(x, 25))
#define SSIG0(x) (rotr32(x,  7) ^ rotr32(x, 18) ^ ((x) >> 3))
#define SSIG1(x) (rotr32(x, 17) ^ rotr32(x, 19) ^ ((x) >> 10))

/* ── 压缩函数: 处理一个 512-bit 块 ──────────────────────────────────────────── */
static void sha256_compress(uint32_t state[8], const uint8_t block[64])
{
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;
    uint32_t w[64];
    int      i;

    /* 1. 消息调度: 将 64 字节块展开为 64 个 32-bit word */
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4 + 0] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] <<  8) |
               ((uint32_t)block[i * 4 + 3] <<  0);
    }
    for (i = 16; i < 64; i++) {
        w[i] = SSIG1(w[i - 2]) + w[i - 7] + SSIG0(w[i - 15]) + w[i - 16];
    }

    /* 2. 初始化工作变量 */
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    /* 3. 主循环: 64 轮压缩 */
    for (i = 0; i < 64; i++) {
        t1 = h + BSIG1(e) + CH(e, f, g) + SHA256_K[i] + w[i];
        t2 = BSIG0(a) + MAJ(a, b, c);
        h  = g; g = f; f = e; e = d + t1;
        d  = c; c = b; b = a; a = t1 + t2;
    }

    /* 4. 更新哈希状态 */
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

/* ── 缓冲区最大输入长度 ────────────────────────────────────────────────────── */
/*
 * buf[192] 所能容纳的最大未 padded 输入长度:
 *   padded = input + 1(0x80) + zeros + 8(length) ≤ 192
 *   1 块 ( ≤  55B) → padded  64B    2 块 ( ≤ 119B) → padded 128B
 *   3 块 ( ≤ 183B) → padded 192B    4 块 ( ≤ 247B) → 需要 256B
 * ATSHA204A 所有用例 ≤ 96 字节, 远在安全范围内。
 */
#define SHA256_BUF_MAX_INPUT  183

/* ── 单次哈希 ──────────────────────────────────────────────────────────────── */
void sha256(const uint8_t *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE])
{
    uint32_t state[8];
    uint8_t  buf[192];          /* 支持最多 183 字节输入 → padded 192 字节 (3 块) */
    size_t   buf_len;
    uint64_t bit_len;
    int      i, n_blocks;

    /* 输入长度保护: 防止 buf[128] 溢出 */
    if (len > SHA256_BUF_MAX_INPUT) {
        rt_kprintf("[SHA256] ERROR: input %u > max %u, buf would overflow\n",
                   (unsigned)len, SHA256_BUF_MAX_INPUT);
        if (digest) rt_memset(digest, 0, SHA256_DIGEST_SIZE);
        return;
    }

    if (data == NULL && len > 0) return;
    if (digest == NULL) return;

    /* 1. 初始化状态 */
    for (i = 0; i < 8; i++) state[i] = SHA256_INIT[i];

    /* 2. 计算 bit 长度 */
    bit_len = (uint64_t)len * 8u;

    /* 3. 构造 padded 消息 */
    /*    Padding 格式: data || 0x80 || zeros || 64-bit big-endian length
     *    总长度 buf_len 必须是 64 字节的整数倍
     */
    /* 3a. 整缓冲区清零, 消灭栈底脏数据 (多块场景 padding 区必须全零) */
    rt_memset(buf, 0, sizeof(buf));

    /* 3b. 复制原始数据 */
    if (len > 0) {
        rt_memcpy(buf, data, len);
    }

    /* 3c. 数据末尾写入 0x80 (bit '1') */
    buf[len] = 0x80;

    /* 3d. 计算对齐到 64 字节的总长度: (len + 1 + 8 + 63) / 64 * 64 */
    buf_len = ((len + 1 + 8 + 63) / 64) * 64;

    /* 3e. 64-bit 长度写入最后 8 字节 (Big-Endian) */
    {
        size_t len_off = buf_len - 8;
        buf[len_off + 0] = (uint8_t)(bit_len >> 56);
        buf[len_off + 1] = (uint8_t)(bit_len >> 48);
        buf[len_off + 2] = (uint8_t)(bit_len >> 40);
        buf[len_off + 3] = (uint8_t)(bit_len >> 32);
        buf[len_off + 4] = (uint8_t)(bit_len >> 24);
        buf[len_off + 5] = (uint8_t)(bit_len >> 16);
        buf[len_off + 6] = (uint8_t)(bit_len >>  8);
        buf[len_off + 7] = (uint8_t)(bit_len >>  0);
    }

    /* 4. 逐块压缩 */
    n_blocks = (int)(buf_len / 64);
    for (i = 0; i < n_blocks; i++) {
        sha256_compress(state, &buf[i * 64]);
    }

    /* 5. 输出摘要 (Big-Endian) */
    for (i = 0; i < 8; i++) {
        digest[i * 4 + 0] = (uint8_t)(state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(state[i] >>  8);
        digest[i * 4 + 3] = (uint8_t)(state[i] >>  0);
    }
}
