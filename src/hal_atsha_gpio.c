/*
 * hal_atsha_gpio.c — ATSHA204A GPIO 单总线 HAL 实现
 * TX: 8N1 波形模拟 0xFF/0xFD
 * RX: 边沿检测 + 脉冲计数 (参考 Microchip hal_swi_gpio.c)
 */

#include "hal_atsha_gpio.h"
#include <rtdevice.h>

#define BIT_CYCLES       ((uint32_t)(SystemCoreClock / 230400U))
#define HALF_BIT_CYCLES  ((uint32_t)(SystemCoreClock / 460800U))

#define SDA_IS_HIGH()    ((PORT()->IDR & PIN()) != 0U)

#define TOKEN_1    0xFFU
#define TOKEN_0    0xFDU

static atsha_hw_gpio_config_t g_cfg;
static rt_bool_t              g_ok = RT_FALSE;

#define PORT()   (g_cfg.sda_port)
#define PIN()    (g_cfg.sda_pin)

/* ── GPIO ───────────────────────────────────────────────── */

static __inline uint8_t pinpos(uint16_t p) { return (uint8_t)__builtin_ctz((uint32_t)p); }

static void gpio_input(void)
    { uint32_t m = PORT()->MODER; uint8_t n = pinpos(PIN());
      PORT()->MODER = m & ~(3UL << (n * 2U)); }

static void gpio_output_od(void)
    { uint32_t m = PORT()->MODER; uint8_t n = pinpos(PIN());
      PORT()->MODER = (m & ~(3UL << (n * 2U))) | (1UL << (n * 2U)); }

/* ── DWT ────────────────────────────────────────────────── */

static void dwt_enable(void)
{
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
    { CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->CYCCNT = 0U; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; }
}

static __inline void wait_bit(void)  { uint32_t s = DWT->CYCCNT; while ((DWT->CYCCNT - s) < BIT_CYCLES) {} }
static __inline void wait_half(void) { uint32_t s = DWT->CYCCNT; while ((DWT->CYCCNT - s) < HALF_BIT_CYCLES) {} }

/* ── TX ─────────────────────────────────────────────────── */

static void tx_8bit(uint8_t byte)
{
    rt_base_t lv = rt_hw_interrupt_disable(); uint8_t b;
    HAL_GPIO_WritePin(PORT(), PIN(), GPIO_PIN_RESET); wait_bit();
    for (b = 0U; b < 8U; b++)
    { HAL_GPIO_WritePin(PORT(), PIN(), (byte & (1U << b)) ? GPIO_PIN_SET : GPIO_PIN_RESET); wait_bit(); }
    HAL_GPIO_WritePin(PORT(), PIN(), GPIO_PIN_SET); wait_bit();
    rt_hw_interrupt_enable(lv);
}

static void byte_to_tokens(uint8_t byte, uint8_t *tokens)
{ uint8_t i; for (i = 0U; i < 8U; i++) tokens[i] = (byte & (1U << i)) ? TOKEN_1 : TOKEN_0; }

/* ── RX: DWT 纯硬件轮询 (零软延时, 参考 Microchip hal_swi_gpio.c) ── */

#define TO_500US_CYC ((uint32_t)(SystemCoreClock / 1000000U) * 500U)  /* 起始沿等待: 覆盖芯片响应帧中的字节间恢复间隙 */
#define TO_46US_CYC  ((uint32_t)(SystemCoreClock / 1000000U) * 46U)

/**
 * @brief 读 1 个 SWI 逻辑位, DWT 纯硬件轮询, 零漏采
 *   Logic 1: LOW(~4µs)+HIGH(~26µs) → 1 上升沿
 *   Logic 0: LOW(~4µs)+HIGH(~34µs)+LOW(~4µs)+HIGH(~4µs) → 2 上升沿
 *   数据窗口 46µs 覆盖 logic 0 完整位时间 (4+34+4+4=46µs)
 * @return 1=逻辑1, 0=逻辑0, -1=超时无应答, -2=跳变沿异常
 */
static int recv_swi_bit_fast(void)
{
    uint32_t start_cyc = DWT->CYCCNT;
    uint8_t  cur, prev = 1U;
    int      edges = 0;

    /* 1. 等待起始下降沿 (最长 500µs, 覆盖芯片响应帧中的字节间恢复间隙;
     *    DWT 纯硬件轮询, 零延时间隔) */
    while (SDA_IS_HIGH())
    {
        if ((DWT->CYCCNT - start_cyc) > TO_500US_CYC) return -1;
    }

    /* 2. 数据检测窗口: 46µs 内统计上升沿个数 (DWT 纯硬件轮询) */
    uint32_t window_start = DWT->CYCCNT;
    prev = 0U;  /* 刚捕获下降沿, 当前状态为低 */

    while ((DWT->CYCCNT - window_start) < TO_46US_CYC)
    {
        cur = SDA_IS_HIGH() ? 1U : 0U;
        if (cur != prev)
        {
            prev = cur;
            if (cur == 1U) edges++;  /* 捕获到上升沿 */
        }
    }

    /* 3. 按官方逻辑判定: 2 上升沿→逻辑 0, 1 上升沿→逻辑 1 */
    if (edges == 2) return 0;
    if (edges == 1) return 1;
    return -2;  /* 跳变沿异常 (0 个或 3+ 个) */
}

/* ═══════════════════════════════════════════════════════════ */
/*  公开 API                                                   */
/* ═══════════════════════════════════════════════════════════ */

rt_err_t hal_atsha_gpio_init(const atsha_hw_gpio_config_t *cfg)
{
    GPIO_InitTypeDef i = {0};
    if (cfg == RT_NULL) { rt_kprintf("[gpio][init] NULL\n"); return -RT_EINVAL; }
    if (g_ok)           { rt_kprintf("[gpio][init] dup\n");  return -RT_EBUSY;  }
    g_cfg = *cfg; dwt_enable();
    i.Pin = PIN(); i.Mode = GPIO_MODE_OUTPUT_OD; i.Pull = GPIO_NOPULL; i.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(PORT(), &i); HAL_GPIO_WritePin(PORT(), PIN(), GPIO_PIN_SET);
    g_ok = RT_TRUE;
    rt_kprintf("[gpio][init] OK %lu cyc\n", (unsigned long)BIT_CYCLES);
    return RT_EOK;
}

rt_err_t hal_atsha_gpio_deinit(void)
{ if (g_ok) { HAL_GPIO_DeInit(PORT(), PIN()); g_ok = RT_FALSE; } return RT_EOK; }

void hal_atsha_gpio_flush(void)
{ if (g_ok) { gpio_output_od(); HAL_GPIO_WritePin(PORT(), PIN(), GPIO_PIN_SET); } }

rt_err_t hal_atsha_gpio_wakeup(void)
{
    rt_base_t lv; if (!g_ok) return -RT_ERROR;

    /* ── DWT 校准: 测量 rt_hw_us_delay 实际耗时 ── */
    {
        uint32_t t1 = DWT->CYCCNT;
        rt_hw_us_delay(10U);
        uint32_t t2 = DWT->CYCCNT;
        rt_kprintf("[gpio] rt_hw_us_delay(10) = %lu cyc (expect %lu)\n",
                   (unsigned long)(t2 - t1), (unsigned long)(SystemCoreClock / 100000U));
    }

    rt_kprintf("[gpio] wakeup\n"); gpio_output_od();
    lv = rt_hw_interrupt_disable();
    HAL_GPIO_WritePin(PORT(), PIN(), GPIO_PIN_RESET); rt_hw_us_delay(80U);
    HAL_GPIO_WritePin(PORT(), PIN(), GPIO_PIN_SET);   rt_hw_interrupt_enable(lv);
    rt_thread_mdelay(3U); hal_atsha_gpio_flush();
    return RT_EOK;
}

rt_err_t hal_atsha_gpio_send(const uint8_t *data, uint16_t len)
{
    uint16_t tc, i; uint8_t *tb;
    if (!g_ok || data == RT_NULL || len == 0U) return -RT_EINVAL;
    tc = (uint16_t)(len * 8U); tb = (uint8_t *)rt_malloc(tc);
    if (tb == RT_NULL) return -RT_ENOMEM;
    for (i = 0U; i < len; i++) byte_to_tokens(data[i], &tb[i * 8U]);
    gpio_output_od(); hal_atsha_gpio_flush();
    for (i = 0U; i < tc; i++) tx_8bit(tb[i]);
    rt_free(tb);
    /*
     * TX 后不延时, 立即切输入 — recv 的起始沿等待 (~95µs) 吸收 tTURNAROUND
     * ⚠ 实测本芯片响应启动 ≈40µs (快于数据手册典型 64-131µs):
     *   任何 TX 后延时 (80µs poll 延时 / 50µs margin) 都会错过响应起始沿,
     *   从帧中间进场导致首字节边沿计数异常。
     */
    gpio_input();
    return RT_EOK;
}

rt_err_t hal_atsha_gpio_recv(uint8_t *buf, uint16_t expect, uint32_t timeout_ms)
{
    uint16_t  bi; uint8_t bit; rt_base_t lv;
    uint32_t  start_cycles = DWT->CYCCNT;
    uint32_t  timeout_cycles = (SystemCoreClock / 1000U) * timeout_ms;
    int       err_byte = -1, err_bit = -1, err_code = 0;

    if (!g_ok || buf == RT_NULL || expect == 0U) return -RT_EINVAL;
    /* 直接切换输入模式 (send 已完成 TX→RX 切换, 此处确保输入状态) */
    gpio_input();

    lv = rt_hw_interrupt_disable();
    for (bi = 0U; bi < expect; bi++)
    {
        uint8_t byte = 0U;
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((DWT->CYCCNT - start_cycles) > timeout_cycles)
            { err_byte = bi; err_bit = bit; err_code = -3; goto exit; }

            int v = recv_swi_bit_fast();
            if (v < 0)
            { err_byte = bi; err_bit = bit; err_code = v; goto exit; }
            if (v == 1) byte |= (uint8_t)(1U << bit);
        }
        buf[bi] = byte;
    }
exit:
    rt_hw_interrupt_enable(lv);

    gpio_output_od(); HAL_GPIO_WritePin(PORT(), PIN(), GPIO_PIN_SET);

    if (err_byte >= 0)
    {
        const char *reason = (err_code == -1) ? "NO_RESP" :
                             (err_code == -2) ? "EDGE_ERR" :
                             (err_code == -3) ? "TIMEOUT" : "UNKN";
        rt_kprintf("[gpio][recv] %s@byte%d.bit%d (got %d bytes): ",
                   reason, err_byte, err_bit, err_byte);
        for (bi = 0U; bi < (uint16_t)err_byte; bi++) rt_kprintf("%02X ", buf[bi]);
        rt_kprintf("\n");
        return -RT_ETIMEOUT;
    }

    rt_kprintf("[gpio][recv] OK: ");
    for (bi = 0U; bi < expect; bi++) rt_kprintf("%02X ", buf[bi]);
    rt_kprintf("\n");
    return RT_EOK;
}

/**
 * @brief 接收完整响应帧 (首字节 = Count, 数据+CRC 一气呵成)
 *
 * 全程关中断, 消除"先收 Count 再收剩余"两次调用之间的采样空窗:
 * 芯片响应是连续位流, 两次调用间隙 (IRQ 窗口 + C 代码耗时) 会漏采
 * 起始下降沿，导致后续 46µs 采样窗口整体错位。
 *
 * @param buf        输出缓冲区 (首字节 = Count, 后随数据+CRC)
 * @param max_len    缓冲区大小
 * @param timeout_ms 整帧超时, 需 ≥ 帧传输时长 (35字节帧 ≈ 13ms),
 *                   芯片忙时靠首字节 edge-wait 快速 NO_RESP 返回
 * @param out_len    输出: 实际帧长 (含 Count)
 * @return RT_EOK 成功, 其他值失败
 */
rt_err_t hal_atsha_gpio_recv_frame(uint8_t *buf, uint16_t max_len,
                                   uint32_t timeout_ms, uint16_t *out_len)
{
    uint16_t  bi; uint8_t bit; rt_base_t lv;
    uint32_t  start_cycles = DWT->CYCCNT;
    uint32_t  timeout_cycles = (SystemCoreClock / 1000U) * timeout_ms;
    int       err_byte = -1, err_bit = -1, err_code = 0;
    uint8_t   count = 0;

    if (!g_ok || buf == RT_NULL || max_len < 3 || out_len == RT_NULL)
        return -RT_EINVAL;

    gpio_input();

    /* 关中断: 从首个下降沿到帧尾全程锁死, 禁止任何中断插入 */
    lv = rt_hw_interrupt_disable();

    /* 1. 读首字节 Count */
    for (bit = 0U; bit < 8U; bit++)
    {
        if ((DWT->CYCCNT - start_cycles) > timeout_cycles)
        { err_byte = 0; err_bit = bit; err_code = -3; goto exit; }

        int v = recv_swi_bit_fast();
        if (v < 0) { err_byte = 0; err_bit = bit; err_code = v; goto exit; }
        if (v == 1) count |= (uint8_t)(1U << bit);
    }
    buf[0] = count;

    /* Count 合法性校验 (防止越界) */
    if (count < 3 || count > max_len)
    { err_byte = 0; err_code = -4; goto exit; }

    /* 2. 连续读取剩余字节 (位流紧密相连, 无需再等起始沿) */
    for (bi = 1U; bi < count; bi++)
    {
        uint8_t byte = 0U;
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((DWT->CYCCNT - start_cycles) > timeout_cycles)
            { err_byte = bi; err_bit = bit; err_code = -3; goto exit; }

            int v = recv_swi_bit_fast();
            if (v < 0) { err_byte = bi; err_bit = bit; err_code = v; goto exit; }
            if (v == 1) byte |= (uint8_t)(1U << bit);
        }
        buf[bi] = byte;
    }

exit:
    rt_hw_interrupt_enable(lv);

    gpio_output_od(); HAL_GPIO_WritePin(PORT(), PIN(), GPIO_PIN_SET);

    if (err_byte >= 0)
    {
        const char *reason = (err_code == -1) ? "NO_RESP" :
                             (err_code == -2) ? "EDGE_ERR" :
                             (err_code == -3) ? "TIMEOUT" :
                             (err_code == -4) ? "BAD_COUNT" : "UNKN";
        rt_kprintf("[gpio][recv] %s@byte%d.bit%d (got %d bytes): ",
                   reason, err_byte, err_bit, err_byte);
        for (bi = 0U; bi < (uint16_t)err_byte; bi++) rt_kprintf("%02X ", buf[bi]);
        rt_kprintf("\n");
        return -RT_ETIMEOUT;
    }

    *out_len = count;
    rt_kprintf("[gpio][recv] OK: ");
    for (bi = 0U; bi < count; bi++) rt_kprintf("%02X ", buf[bi]);
    rt_kprintf("\n");
    return RT_EOK;
}
