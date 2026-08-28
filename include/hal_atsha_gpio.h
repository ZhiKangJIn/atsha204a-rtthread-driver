/*
 * hal_atsha_gpio.h — ATSHA204A GPIO 单总线硬件抽象层
 *
 * 层定位
 * 本层是三层驱动架构的最底层: 使用 GPIO 模拟 UART 8N1 发送 0xFF/0xFD 波形
 * 对上 (atsha_core.c) 屏蔽所有硬件细节。
 * 引脚/端口通过 atsha_hw_gpio_config_t 由上层注入, 驱动内部不硬编码。
 *
 * 与 hal_atsha.h (UART全双工) 的区别:
 *   - 仅使用单根 GPIO (PB12) 实现半双工通信
 *   - GPIO 位打直接复现 UART 8N1 的 0xFF/0xFD 波形 (与硬件 UART 输出等价)
 *   - 发送: GPIO 开漏输出 + DWT 精确定时位打
 *   - 接收: GPIO 输入模式 + 每 token 独立检测 Start↓ + 中心采样8位
 *   - Token 编解码与 hal_atsha.c 完全一致 (byte_to_tokens / tokens_to_byte)
 *   - 唤醒: 同原方案, SDA 引脚临时拉低 80µs
 *
 * 核心职责
 *   1. GPIO 设备管理: 通过注入的引脚配置初始化单总线
 *   2. 唤醒时序:     GPIO 开漏拉低 80µs → 等待 3ms
 *   3. 数据收发:     byte ↔ 0xFF/0xFD token ↔ GPIO 8N1 波形
 *   4. 总线清洗:     发送前/唤醒后清空残留
 */

#ifndef __HAL_ATSHA_GPIO_H__
#define __HAL_ATSHA_GPIO_H__

#include <rtthread.h>
#include <stdint.h>
#include <board.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ATSHA_GPIO_BAUDRATE          230400  /* 单总线波特率 */
#define ATSHA_GPIO_BIT_TIME_US       4       /* 每 bit 延时 (µs), 1/230400≈4.34µs */
#define ATSHA_GPIO_RECALL_TIMEOUT_MS 10      /* 回声接收超时 (毫秒) */

/* GPIO 单总线硬件配置 */
typedef struct {
    GPIO_TypeDef *sda_port;      /* 单总线 GPIO 端口, 如 GPIOB    */
    uint16_t      sda_pin;       /* 单总线 GPIO 引脚, 如 GPIO_PIN_12 */
} atsha_hw_gpio_config_t;

/* 设备初始化 */
rt_err_t hal_atsha_gpio_init(const atsha_hw_gpio_config_t *config);
rt_err_t hal_atsha_gpio_deinit(void);

/* 唤醒 ATSHA204A 芯片 */
rt_err_t hal_atsha_gpio_wakeup(void);

/* 数据收发 */
rt_err_t hal_atsha_gpio_send(const uint8_t *data, uint16_t len);
rt_err_t hal_atsha_gpio_recv(uint8_t *buffer, uint16_t expect_len, uint32_t timeout_ms);
/* 整帧接收（首字节为 Count，数据和 CRC 连续接收，全程关中断）。 */
rt_err_t hal_atsha_gpio_recv_frame(uint8_t *buf, uint16_t max_len,
                                   uint32_t timeout_ms, uint16_t *out_len);

/* 总线数据清理 */
void hal_atsha_gpio_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_ATSHA_GPIO_H__ */
