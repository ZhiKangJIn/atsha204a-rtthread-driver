/*
 * atsha_core.h — ATSHA204A 协议层
 *
 * 层定位:
 * 本层位于硬件抽象层之上，负责 ATSHA204A 通信协议的编解码
 * 对上 (atsha_api.c) 提供语义化的事务接口
 * 对下 (hal_atsha_gpio.c) 调用字节流收发
 *
 * 数据格式:
 * TX: <Flag 0x77> <Count> <Opcode> <Param1> <Param2_L> <Param2_H> <Data...> <CRC_L> <CRC_H>
 * RX: <Count> <Data/Status...> <CRC_L> <CRC_H>
 *
 * 数据包说明:
 * - Count 包含自身 + 后续所有字节 (含 CRC), 不含 Flag
 * - CRC-16: 多项式 0x8005, 初始值 0x0000, LSB-first, 对 Count 到 Data 末尾计算
 * - RX 无 Flag 字节
 */

#ifndef __ATSHA_CORE_H__
#define __ATSHA_CORE_H__

#include <rtthread.h>
#include <stdint.h>
#include "hal_atsha_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ATSHA204A 命令码 */
#define ATSHA_OPCODE_PAUSE       0x01
#define ATSHA_OPCODE_READ        0x02
#define ATSHA_OPCODE_MAC         0x08
#define ATSHA_OPCODE_HMAC        0x11
#define ATSHA_OPCODE_WRITE       0x12
#define ATSHA_OPCODE_GENDIG      0x15
#define ATSHA_OPCODE_NONCE       0x16
#define ATSHA_OPCODE_LOCK        0x17
#define ATSHA_OPCODE_RANDOM      0x1B
#define ATSHA_OPCODE_DERIVEKEY   0x1C
#define ATSHA_OPCODE_UPDATEEXTRA 0x20
#define ATSHA_OPCODE_CHECKMAC    0x28
#define ATSHA_OPCODE_DEVREV      0x30
#define ATSHA_OPCODE_SHA         0x47

/* 芯片状态码 */
#define ATSHA_STATUS_SUCCESS         0x00
#define ATSHA_STATUS_CHECKMAC_FAIL   0x01
#define ATSHA_STATUS_PARSE_ERROR     0x03
#define ATSHA_STATUS_EXEC_ERROR      0x0F
#define ATSHA_STATUS_AFTER_WAKE      0x11
#define ATSHA_STATUS_COMM_ERROR      0xFF

/* 单线接口特殊命令字/标志 */
#define ATSHA_COMMAND_FLAG   0x77   /* 命令标志 */
#define ATSHA_TRANSMIT_FLAG  0x88   /* 传输标志 */
#define ATSHA_POWER_LOW      0xCC   /* 休眠模式 */
#define ATSHA_IDLE           0xBB   /* 空闲模式 */

/* Zone 编码 */
#define ATSHA_ZONE_CONFIG      0x00   /* 配置区   */
#define ATSHA_ZONE_OTP         0x01   /* OTP 区   */
#define ATSHA_ZONE_DATA        0x02   /* 数据区   */

/* 块大小 */
#define ATSHA_BLOCK_4B         0x00   /*  4 字节字写入   */
#define ATSHA_BLOCK_32B        0x80   /* 32 字节块写入   */

/* 加密标志 */
#define ATSHA_WRITE_PLAIN      0x00   /* 明文写入 */
#define ATSHA_WRITE_ENCRYPT    0x40   /* 加密写入 */

/* Lock 命令 Param1 */
#define ATSHA_LOCK_ZONE_CONFIG     0x00   /* Zone=Config, 校验 CRC       */
#define ATSHA_LOCK_ZONE_DATA_OTP   0x01   /* Zone=Data+OTP, 校验 CRC     */
#define ATSHA_LOCK_IGNORE_CRC      0x80   /* Bit7=1 跳过 CRC */

/* Nonce 命令 */
/* Mode[1:0]: 00=Combine(Rand), 01=Combine(NoSeed), 10=Reserved, 11=Pass-through */
#define ATSHA_NONCE_MODE_COMBINE      0x00   /* 组合模式: SHA-256(RandOut||NumIn||Op||Mode||0x00)   */
#define ATSHA_NONCE_MODE_COMBINE_NOSD 0x01   /* 组合模式, 不更新 EEPROM Seed                        */
#define ATSHA_NONCE_MODE_PASSTHROUGH  0x03   /* 直通: 32B 直接写入 TempKey, SourceFlag=Input         */
                                             /* ⚠ 直通模式 TempKey 不可用于 Read/Write 加密操作      */
#define ATSHA_NONCE_NUMIN_LEN         20     /* Combine 模式要求的 NumIn 字节数                      */

/* MAC 命令 Mode (Param1) */
/* Bit0: 0=第二32B来自Challenge输入, 1=第二32B来自TempKey                        */
/* Bit1: 0=第一32B来自Data Slot,      1=第一32B来自TempKey                        */
/* Bit4: 0=OTP[8:10]置零, 1=包含     Bit5: 0=OTP[0:7]置零, 1=包含               */
/* Bit6: 0=SN[2:3]/SN[4:7]置零, 1=包含                                         */
#define ATSHA_MAC_MODE_CHALLENGE  0x00   /* 第一=SK,  第二=Challenge → 需传入 32B 数据             */
#define ATSHA_MAC_MODE_TEMPKEY    0x01   /* 第一=SK,  第二=TempKey   → 挑战-应答认证               */
#define ATSHA_MAC_MODE_BOTH_TK    0x03   /* 第一=TK,  第二=TempKey   → 较少使用                    */
#define ATSHA_MAC_INCLUDE_OTP     0x10   /* Bit4=1: MAC 消息包含 OTP[8:10] (叠加到 Mode)           */
#define ATSHA_MAC_INCLUDE_SN_EXT  0x40   /* Bit6=1: MAC 消息包含 SN[2:3]/SN[4:7] (叠加到 Mode)    */

/* GenDig 命令 Param1 (直接的 Zone 值, 非位域) */
#define ATSHA_GENDIG_ZONE_CONFIG    0x00   /* 配置区 (SlotID=0/1 选择前两个 256-bit 块) */
#define ATSHA_GENDIG_ZONE_OTP       0x01   /* OTP 区                                */
#define ATSHA_GENDIG_ZONE_DATA      0x02   /* Data 区                               */
#define ATSHA_GENDIG_KEYID_MASK     0x0F   /* Param2 低 4 位为 KeyID (Slot 0-15)    */

/* Read 命令 Param1 组合宏 */
/* 不使用: 芯片 EncryptRead=1 时不需额外 Bit6 标志, TempKey.SourceFlag 决定加密 */
#define ATSHA_READ_4B_CONFIG   (ATSHA_BLOCK_4B  | ATSHA_ZONE_CONFIG)  /* 0x00 */
#define ATSHA_READ_32B_CONFIG  (ATSHA_BLOCK_32B | ATSHA_ZONE_CONFIG)  /* 0x80 */
#define ATSHA_READ_32B_OTP     (ATSHA_BLOCK_32B | ATSHA_ZONE_OTP)     /* 0x81 */
#define ATSHA_READ_4B_DATA     (ATSHA_BLOCK_4B  | ATSHA_ZONE_DATA)    /* 0x02 */
#define ATSHA_READ_32B_DATA    (ATSHA_BLOCK_32B | ATSHA_ZONE_DATA)    /* 0x82 */

/* 配置区字地址 (Param2, 4-Byte Read) */
#define ATSHA_SN_WORD_ADDR_0    0x00   /* SN<0:3>           */
#define ATSHA_SN_WORD_ADDR_2    0x02   /* SN<4:7>           */
#define ATSHA_SN_WORD_ADDR_3    0x03   /* SN<8> 在 byte 0   */

/* Write 命令 Param1 组合宏 */
/* 配置区写入 (工厂 provisioning 用)                                             */
#define ATSHA_WRITE_4B_CONFIG_PLAIN   (ATSHA_BLOCK_4B  | ATSHA_WRITE_PLAIN  | ATSHA_ZONE_CONFIG) /* 0x00 */
#define ATSHA_WRITE_32B_CONFIG_PLAIN  (ATSHA_BLOCK_32B | ATSHA_WRITE_PLAIN  | ATSHA_ZONE_CONFIG) /* 0x80 */
/* OTP 明文扣次                                                   §4.4 v1.6    */
#define ATSHA_WRITE_32B_OTP_PLAIN  (ATSHA_BLOCK_32B | ATSHA_WRITE_PLAIN  | ATSHA_ZONE_OTP)  /* 0x81 */
/* Data Zone 明文写入 (工厂 Slot 0/1 初始化)                                     */
#define ATSHA_WRITE_32B_DATA_PLAIN    (ATSHA_BLOCK_32B | ATSHA_WRITE_PLAIN  | ATSHA_ZONE_DATA)   /* 0x82 */
/* Slot 1 加密写入                                              §4.6           */
#define ATSHA_WRITE_32B_DATA_ENC   (ATSHA_BLOCK_32B | ATSHA_WRITE_ENCRYPT | ATSHA_ZONE_DATA) /* 0xC2 */

/* 命令执行时间 */
#define ATSHA_EXEC_NONCE       22   /* Nonce mode 0/1: 典型22ms, 最大60ms   */
#define ATSHA_EXEC_MAC         12   /* MAC: 典型12ms, 最大35ms              */
#define ATSHA_EXEC_GENDIG      11   /* GenDig: 典型11ms, 最大43ms           */
#define ATSHA_EXEC_READ        0.4   /* Read: 典型0.4ms, 最大4ms             */
#define ATSHA_EXEC_WRITE       4   /* Write: 典型4ms, 最大数据手册标42ms*/
#define ATSHA_EXEC_LOCK        5   /* Lock: 典型5ms, 最大24ms              */
#define ATSHA_EXEC_RANDOM      11   /* Random: 典型11ms, 最大50ms           */
#define ATSHA_TIMEOUT_DEFAULT  70   /* 建议默认超时 ≥ 最大命令时间(69ms)     */
#define ATSHA_TIMEOUT_READ    200   /* Read 命令轮询超时 (ms)               */
#define ATSHA_TIMEOUT_WRITE   200   /* Write 命令轮询超时 (ms)              */
#define ATSHA_TIMEOUT_LOCK    200   /* Lock 命令轮询超时 (ms)               */

#define CRC16_REG_INIT 0x0000
#define CRC16_POLY     0x8005

// CRC-16 校验
uint16_t atsha_core_crc16(const uint8_t *data, size_t len);

// 命令打包/解包
rt_err_t atsha_core_pack(uint8_t *tx_buf, size_t tx_buf_max,
                         uint8_t opcode, uint8_t param1, uint16_t param2,
                         const uint8_t *data_in, size_t data_in_len,
                         size_t *packed_len);

rt_err_t atsha_core_unpack(const uint8_t *rx_data, size_t rx_len,
                           uint8_t *payload, size_t *payload_len,
                           int *status_code);

// 事务包装
rt_err_t atsha_core_transaction(uint8_t opcode, uint8_t param1,
                                uint16_t param2, const uint8_t *tx_data,
                                size_t tx_len, uint8_t *rx_buf,
                                size_t *rx_len,
                                uint32_t execution_ms, uint32_t timeout_ms);

// 命令包装
rt_err_t atsha_core_nonce(uint8_t mode, const uint8_t *num_in,
                          uint8_t rand_out[32]);

rt_err_t atsha_core_mac(uint8_t mode, uint16_t slot_id,
                        uint8_t mac_out[32]);

rt_err_t atsha_core_gendig(uint8_t zone, uint16_t key_id);

rt_err_t atsha_core_read(uint8_t param1, uint16_t block_addr,
                         uint8_t *data_out, size_t *data_len);

rt_err_t atsha_core_write(uint8_t param1, uint16_t block_addr,
                          const uint8_t *data, size_t data_len);

rt_err_t atsha_core_lock(uint8_t zone, uint16_t crc);

rt_err_t atsha_core_random(uint8_t rand_out[32]);

/* 芯片状态控制 */
rt_err_t atsha_core_wakeup(void);
rt_err_t atsha_core_enter_low_power(void);
rt_err_t atsha_core_enter_idle(void);

#ifdef __cplusplus
}
#endif

#endif /* __ATSHA_CORE_H__ */
