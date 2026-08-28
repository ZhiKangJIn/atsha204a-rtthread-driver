/*
 * atsha_core.c — ATSHA204A 协议层 实现
 */

#include "atsha_core.h"
/*
 * GPIO 单总线版本: hal_atsha_gpio_* 函数通过 atsha_core.h → hal_atsha_gpio.h 引入
 * 旧 UART 全双工版本 (hal_atsha.h/c) 保留但不调用
 */

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  命令包装函数                                                              */
/* ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Nonce 命令 — 产生 32 字节硬件随机数并更新 TempKey
 * @param mode      模式 (0x00=Combine, 0x03=PassThrough)
 * @param num_in    输入随机数 (mode=0: 20B, mode=3: 32B)
 * @param rand_out  输出: 32 字节随机数
 * @return RT_EOK 成功, 其他值失败
 */
rt_err_t atsha_core_nonce(uint8_t mode, const uint8_t *num_in,
                          uint8_t rand_out[32])
{
    size_t rx_len = 32;
    size_t num_in_len;

    if (rand_out == RT_NULL || num_in == RT_NULL)
        return -RT_EINVAL;

    /* 根据模式确定 NumIn 长度 */
    if (mode == ATSHA_NONCE_MODE_PASSTHROUGH)
        num_in_len = 32;
    else
        num_in_len = ATSHA_NONCE_NUMIN_LEN;  /* 20 */

    return atsha_core_transaction(
        ATSHA_OPCODE_NONCE, mode, 0x0000,
        num_in, num_in_len,
        rand_out, &rx_len,
        ATSHA_EXEC_NONCE, ATSHA_TIMEOUT_DEFAULT);
}

/**
 * @brief MAC 命令 — 计算并返回 32 字节消息认证码
 * @param mode    Mode (0x01 = 挑战-应答模式: 第一=SK, 第二=TempKey)
 * @param slot_id 使用的密钥槽编号 (0-15)
 * @param mac_out 输出: 32 字节 MAC 值
 * @return RT_EOK 成功, 其他值失败
 */
rt_err_t atsha_core_mac(uint8_t mode, uint16_t slot_id,
                        uint8_t mac_out[32])
{
    size_t rx_len = 32;

    if (mac_out == RT_NULL)
        return -RT_EINVAL;

    /* Mode=0x01: 第二 32B 来自 TempKey, 无需传入 Challenge 数据 */
    return atsha_core_transaction(
        ATSHA_OPCODE_MAC, mode, slot_id,
        RT_NULL, 0,
        mac_out, &rx_len,
        ATSHA_EXEC_MAC, ATSHA_TIMEOUT_DEFAULT);
}

/**
 * @brief GenDig 命令 — 用指定槽位密钥更新 TempKey (无输出数据)
 * @param zone   GenDig 区域 (ATSHA_GENDIG_ZONE_CONFIG/OTP/DATA)
 * @param key_id 密钥槽编号 (低 4 位有效)
 * @return RT_EOK 成功, 其他值失败
 */
rt_err_t atsha_core_gendig(uint8_t zone, uint16_t key_id)
{
    size_t  rx_len = 0;
    uint8_t dummy;

    /* GenDig 仅返回状态码, 无 payload 数据 */
    return atsha_core_transaction(
        ATSHA_OPCODE_GENDIG, zone, key_id,
        RT_NULL, 0,
        &dummy, &rx_len,
        ATSHA_EXEC_GENDIG, ATSHA_TIMEOUT_DEFAULT);
}

/**
 * @brief Read 命令 — 读取配置/OTP/数据区
 * @param param1     Param1 字节 (Zone + BlockSize 标志组合)
 * @param block_addr 块地址 (Param2)
 * @param data_out   输出缓冲区
 * @param data_len   输入为缓冲区大小, 输出为实际数据长度
 * @return RT_EOK 成功, 其他值失败
 */
rt_err_t atsha_core_read(uint8_t param1, uint16_t block_addr,
                         uint8_t *data_out, size_t *data_len)
{
    if (data_out == RT_NULL || data_len == RT_NULL)
        return -RT_EINVAL;

    return atsha_core_transaction(
        ATSHA_OPCODE_READ, param1, block_addr,
        RT_NULL, 0,
        data_out, data_len,
        ATSHA_EXEC_READ, ATSHA_TIMEOUT_READ);
}

/**
 * @brief Write 命令 — 写入配置/OTP/数据区 (明文或加密+MAC)
 * @param param1     Param1 字节 (Zone + BlockSize + Encrypt 标志组合)
 * @param block_addr 块地址 (Param2)
 * @param data       待写入数据 (明文: 4B/32B; 加密: 32B密文+32B MAC)
 * @param data_len   数据长度
 * @return RT_EOK 成功, 其他值失败
 */
rt_err_t atsha_core_write(uint8_t param1, uint16_t block_addr,
                          const uint8_t *data, size_t data_len)
{
    size_t  rx_len = 0;
    uint8_t dummy;

    if (data == RT_NULL || data_len == 0)
        return -RT_EINVAL;

    /* Write 仅返回状态码, 无 payload 数据 */
    return atsha_core_transaction(
        ATSHA_OPCODE_WRITE, param1, block_addr,
        data, data_len,
        &dummy, &rx_len,
        ATSHA_EXEC_WRITE, ATSHA_TIMEOUT_WRITE);
}

/**
 * @brief Lock 命令 — 锁定配置区或数据+OTP 区 (不可逆)
 * @param zone 锁定区域 (ATSHA_LOCK_ZONE_CONFIG 或 ATSHA_LOCK_ZONE_DATA_OTP)
 * @param crc  目标区域的 CRC-16 校验和
 * @return RT_EOK 成功, 其他值失败
 */
rt_err_t atsha_core_lock(uint8_t zone, uint16_t crc)
{
    size_t  rx_len = 0;
    uint8_t dummy;

    return atsha_core_transaction(
        ATSHA_OPCODE_LOCK, zone, crc,
        RT_NULL, 0,
        &dummy, &rx_len,
        ATSHA_EXEC_LOCK, ATSHA_TIMEOUT_LOCK);
}

/**
 * @brief Random 命令 — 生成 32 字节硬件随机数
 * @param rand_out 输出: 32 字节随机数
 * @return RT_EOK 成功, 其他值失败
 */
rt_err_t atsha_core_random(uint8_t rand_out[32])
{
    size_t rx_len = 32;

    if (rand_out == RT_NULL)
        return -RT_EINVAL;

    return atsha_core_transaction(
        ATSHA_OPCODE_RANDOM, 0x00, 0x0000,
        RT_NULL, 0,
        rand_out, &rx_len,
        ATSHA_EXEC_RANDOM, ATSHA_TIMEOUT_DEFAULT);
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  芯片状态控制                                                              */
/* ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief 发送传输指令数据 (Transmit Flag 0x88)
 * @note 用于轮询 ATSHA204A 是否就绪, 芯片收到后返回状态包或数据包
 */
static rt_err_t atsha_core_send_command(void)
{
    uint8_t cmd = ATSHA_TRANSMIT_FLAG;
    return hal_atsha_gpio_send(&cmd, 1);
}

/**
 * @brief 完整的 ATSHA204A 唤醒握手
 *
 * 流程:
 *   1. hal_atsha_gpio_wakeup()    — TX 拉低 80µs, 芯片从 Sleep 进入 Idle
 *   2. hal_atsha_gpio_flush()     — 清空回声残留 (ring buffer + 信号量)
 *   3. hal_atsha_send(0x88)  — 发送 Transmit Flag 轮询芯片
 *   4. hal_atsha_gpio_recv(4B)    — 接收 after-wake 状态包
 *   5. 验证响应               — 期望 0x04 0x11 0x33 0x43 (Count=4, Status=AfterWake)
 *
 * @return RT_EOK 唤醒成功, 其他值失败
 * @note  必须成对执行 wakeup + 0x88 + recv, 否则 after-wake 响应会残留在
 *        芯片输出缓冲区, 干扰后续命令的 0x88 轮询
 */
rt_err_t atsha_core_wakeup(void)
{
    rt_err_t ret;
    uint8_t  rx_buf[4];
    static const uint8_t EXPECTED[] = { 0x04, 0x11, 0x33, 0x43 };

    /* Step 1: 物理唤醒脉冲 */
    ret = hal_atsha_gpio_wakeup();
    if (ret != RT_EOK)
    {
        rt_kprintf("[Core][Wakeup] 唤醒脉冲失败 (%d)\n", ret);
        return ret;
    }

    /* Step 2: 清空回声残留 */
    hal_atsha_gpio_flush();

    /* Step 3: 发送 Transmit Flag (0x88) */
    ret = atsha_core_send_command();  /* → hal_atsha_send(&0x88, 1) */
    if (ret != RT_EOK)
    {
        rt_kprintf("[Core][Wakeup] 发送 0x88 失败 (%d)\n", ret);
        return ret;
    }

    /* Step 4: 接收 after-wake 响应 */
    ret = hal_atsha_gpio_recv(rx_buf, 4, 200);
    if (ret != RT_EOK)
    {
        rt_kprintf("[Core][Wakeup] 接收 after-wake 响应超时 (%d)\n", ret);
        return ret;
    }

    rt_kprintf("[Core][Wakeup] 响应: 0x%02X 0x%02X 0x%02X 0x%02X\n",
               rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3]);

    /* Step 5: 验证 */
    if (rx_buf[0] != EXPECTED[0] || rx_buf[1] != EXPECTED[1] ||
        rx_buf[2] != EXPECTED[2] || rx_buf[3] != EXPECTED[3])
    {
        rt_kprintf("[Core][Wakeup] 响应不匹配: 期望 0x04 0x11 0x33 0x43\n");
        return -RT_EIO;
    }

    rt_kprintf("[Core][Wakeup] 握手完成, 芯片已就绪\n");
    return RT_EOK;
}

/**
 * @brief 使 ATSHA204A 进入低功耗休眠模式 (Sleep - 0xCC)
 * @note 会清空 SRAM 中的所有状态，包括 TempKey
 */
rt_err_t atsha_core_enter_low_power(void)
{
    uint8_t cmd = ATSHA_POWER_LOW;
    return hal_atsha_gpio_send(&cmd, 1);
}

/**
 * @brief 使 ATSHA204A 进入空闲模式 (Idle - 0xBB)
 * @note 会复位看门狗，但保留 SRAM 中的 TempKey 和 RNG Seed
 */
rt_err_t atsha_core_enter_idle(void)
{
    uint8_t cmd = ATSHA_IDLE;
    return hal_atsha_gpio_send(&cmd, 1);
}

/**
 * @brief 状态码转字符串辅助函数
 */
const char *atsha_core_status_str(int status_code)
{
    switch (status_code)
    {
        case ATSHA_STATUS_SUCCESS:         return "SUCCESS";
        case ATSHA_STATUS_CHECKMAC_FAIL:   return "CHECKMAC_FAIL";
        case ATSHA_STATUS_PARSE_ERROR:     return "PARSE_ERROR";
        case ATSHA_STATUS_EXEC_ERROR:      return "EXEC_ERROR";
        case ATSHA_STATUS_AFTER_WAKE:      return "AFTER_WAKE";
        case ATSHA_STATUS_COMM_ERROR:      return "COMM_ERROR";
        default:                           return "UNKNOWN_ERROR";
    }
}

/**
 * @brief  计算 ATSHA204A CRC-16 校验码
 * @param  data 要计算的数据缓冲区指针 (注：不包含 Command Flag 0x77)
 * @param  len  要计算的数据长度 (包含 Count、Opcode、Param1、Param2 和 Data)
 * @return 16位 CRC 计算结果
 */
uint16_t atsha_core_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc_reg = 0x0000;  // 芯片要求的初始值
    uint16_t poly = 0x8005;     // 芯片使用的多项式

    for (size_t i = 0; i < len; i++)
    {
        // ATSHA204A 要求数据字节必须从 LSB 到 MSB 逐位处理
        for (uint8_t bit_mask = 0x01; bit_mask != 0x00; bit_mask <<= 1)
        {
            uint8_t data_bit = (data[i] & bit_mask) ? 1 : 0;
            uint8_t crc_bit = (crc_reg >> 15) & 0x01;
            crc_reg <<= 1;
            if (data_bit != crc_bit)
            {
                crc_reg ^= poly;
            }
        }
    }

    return crc_reg;
}

/**
 * @brief  将命令参数和数据组装成标准的 ATSHA204A 物理层发送数据包
 * @param tx_buf       输出缓冲区指针
 * @param tx_buf_max   输出缓冲区最大长度
 * @param opcode       命令码
 * @param param1       参数1
 * @param param2       参数2
 * @param data_in      输入数据指针
 * @param data_in_len  输入数据长度
 * @param packed_len   返回组装后的数据包长度
 * @return             操作结果
 */
rt_err_t atsha_core_pack(uint8_t *tx_buf, size_t tx_buf_max,
                         uint8_t opcode, uint8_t param1, uint16_t param2,
                         const uint8_t *data_in, size_t data_in_len,
                         size_t *packed_len)
{
    if (tx_buf == RT_NULL || packed_len == RT_NULL) return -RT_EINVAL;

    // 计算 Count：1(Count) + 1(Opcode) + 1(Param1) + 2(Param2) + DataLen + 2(CRC)
    uint8_t count = 1 + 1 + 1 + 2 + data_in_len + 2;
    // 加上最前面的 Command Flag (0x77)，总空间需要 count + 1
    if ((count + 1) > tx_buf_max)
    {
        rt_kprintf("[core][pack]: 发送数据包超出输出缓冲区大小，请扩大输出缓冲区\n");
        return -RT_ENOMEM;
    }

    size_t idx = 0;

    // 核心组装逻辑
    tx_buf[idx++] = ATSHA_COMMAND_FLAG;              // Command Flag
    tx_buf[idx++] = count;
    tx_buf[idx++] = opcode;
    tx_buf[idx++] = param1;
    tx_buf[idx++] = (uint8_t)(param2 & 0x00FF);        // Param2 低字节
    tx_buf[idx++] = (uint8_t)((param2 >> 8) & 0x00FF);   // Param2 高字节

    // 填入核心数据
    if (data_in != RT_NULL && data_in_len > 0)
    {
        rt_memcpy(&tx_buf[idx], data_in, data_in_len);
        idx += data_in_len;
    }
    else
    {
        // 如果没有数据则说明是状态包
        // 不需要填入数据
    }

    // 计算并填入 CRC 校验码
    uint16_t crc_val = atsha_core_crc16(&tx_buf[1], count - 2);
    tx_buf[idx++] = (uint8_t)(crc_val & 0x00FF);        // CRC 低字节
    tx_buf[idx++] = (uint8_t)((crc_val >> 8) & 0x00FF);   // CRC 高字节

    *packed_len = idx; // 最终返回包的总长度 (Count + 1)
    return RT_EOK;
}

/**
 * @brief  解析 ATSHA204A 响应帧, 校验 CRC, 提取 payload 和状态码
 * @param rx_data      输入的响应数据指针
 * @param rx_len       输入的响应数据长度
 * @param payload      输出的数据地址指针
 * @param payload_len  输出的数据长度
 * @param status_code  输出的状态码指针
 * @return             操作结果
 * @note    该函数主要将从机数据引入进行检查并且提取有用信息（状态码或者数据）
 */
rt_err_t atsha_core_unpack(const uint8_t *rx_data, size_t rx_len,
                           uint8_t *payload, size_t *payload_len,
                           int *status_code)
{
    // 参数检查
    if(rx_data == RT_NULL){rt_kprintf("[core][unpack]: 输入的响应数据指针为空\n"); return -RT_EINVAL;}
    else if(rx_len == 0){rt_kprintf("[core][unpack]: 输入的响应数据长度为0\n"); return -RT_EINVAL;}
    else if(payload == RT_NULL){rt_kprintf("[core][unpack]: 输出的数据地址指针为空\n"); return -RT_EINVAL;}
    else if(payload_len == RT_NULL){rt_kprintf("[core][unpack]: 输出的数据长度指针为空\n"); return -RT_EINVAL;}
    else if(status_code == RT_NULL){rt_kprintf("[core][unpack]: 输出的状态码指针为空\n"); return -RT_EINVAL;}

    /* 至少需要 3 字节: Count + CRC_L + CRC_H */
    if (rx_len < 3) { rt_kprintf("[core][unpack]: 输入的响应数据长度过小\n"); return -RT_EINVAL; }

    uint8_t count = rx_data[0];

    /* Count 验证 */
    if (count != rx_len)
    {
        rt_kprintf("[core][unpack]: 响应数据长度不匹配\n", count, rx_len);
        return -RT_EIO;
    }

    /* 纯数据长度等于总包长减去 Count(1字节) 和 CRC(2字节) */
    size_t plen = count - 3;

    /* CRC 校验 */
    uint16_t crc_calc  = atsha_core_crc16(rx_data, count - 2);
    uint16_t crc_frame = (uint16_t)rx_data[count - 2] |
                         ((uint16_t)rx_data[count - 1] << 8);

    if (crc_calc != crc_frame)
    {
        rt_kprintf("[core][unpack]: CRC 不匹配\n", crc_calc, crc_frame);
        // 如果 CRC 校验失败，说明数据在传输过程中可能被篡改或损坏
        *status_code = ATSHA_STATUS_COMM_ERROR; //手动设置状态码到通信损坏
        return -RT_EIO;
    }

    /* ── 状态包判断 ── */
    if (count == 4)
    {
        uint8_t device_status = rx_data[1];
        *status_code = device_status;
        *payload_len = 0;

        if (device_status != ATSHA_STATUS_SUCCESS && device_status != ATSHA_STATUS_AFTER_WAKE)
        {
            rt_kprintf("[core][unpack]: 从机设备状态异常: 0x%02X\n", device_status);
            return -RT_ERROR;
        }

        rt_kprintf("[core][unpack]: 从机设备状态正常: 0x%02X\n", device_status);
        return RT_EOK;
    }

    /* ── 数据包提取 ── */
    if (plen > *payload_len) {
        rt_kprintf("[core][unpack]: 提取到的数据包长度超过输出缓冲区大小\n");
        return -RT_ENOMEM;
    }

    rt_memcpy(payload, &rx_data[1], plen);
    *payload_len = plen;
    *status_code = ATSHA_STATUS_SUCCESS;
    rt_kprintf("[core][unpack]: 提取到数据包，长度为 %zu\n", plen);
    return RT_EOK;
}

/**
 * @brief  执行一条完整的 ATSHA204A 事务
 *
 * 流程:
 *   1. 组包              (Pack)
 *   2. 发送命令包         (Send Command Packet)
 *   3. Wait + Poll 握手   (Datasheet §7.1.2, p24)
 *      - Wait: 等待 execution_ms, 芯片执行命令期间不碰总线
 *      - Poll: 发送 0x88 + 尝试收整帧 (Count+数据+CRC 一气呵成)
 *              若未就绪则等 1ms 再敲门, 直至 timeout_ms 耗尽
 *   4. 整帧接收          (Recv Frame, 全程关中断)
 *   5. 拆包 + CRC + 状态  (Unpack)
 *
 * @param execution_ms  芯片命令执行典型最大时间 (ms), Wait 阶段
 * @param timeout_ms    轮询超时容限 (ms), Poll 阶段
 */
rt_err_t atsha_core_transaction(uint8_t opcode, uint8_t param1, uint16_t param2,
                                const uint8_t *tx_data, size_t tx_len,
                                uint8_t *rx_buf, size_t *rx_len,
                                uint32_t execution_ms, uint32_t timeout_ms)
{
    rt_err_t  ret;
    int       status = ATSHA_STATUS_SUCCESS;
    uint8_t   tx_pkt[128];
    uint8_t   rx_raw[128];
    uint8_t   count = 0;
    size_t    tx_pkt_len;

    // 边界分析
    if (rx_buf == RT_NULL || rx_len == RT_NULL)
        return -RT_EINVAL;

    // 组包
    ret = atsha_core_pack(tx_pkt, sizeof(tx_pkt),
                          opcode, param1, param2,
                          tx_data, tx_len, &tx_pkt_len);
    if (ret != RT_EOK) return ret;

    // 发送命令包
    ret = hal_atsha_gpio_send(tx_pkt, tx_pkt_len);
    if (ret != RT_EOK)
    {
        rt_kprintf("协议层：发送失败 (%d)\n", ret);
        return ret;
    }

    /*
     * ══════ Wait + Poll 混合策略 (Datasheet §7.1.2, p24) ══════
     *
     *   Wait 阶段: 先等 execution_ms (芯片命令执行典型最大时间),
     *              此期间芯片忙, 不碰总线, 避免无效轮询和日志噪音。
     *   Poll 阶段: 发送 0x88 → 尝试接收完整帧 (Count+数据+CRC,
     *              全程关中断一气呵成), 若未就绪则等 1ms 再敲门,
     *              直至 timeout_ms 耗尽。
     */
    {
        rt_tick_t deadline;
        rt_bool_t got_count = RT_FALSE;
        uint16_t  frame_len = 0;

        /* Wait 阶段: 等芯片执行完命令, 期间不碰总线 */
        if (execution_ms > 0)
            rt_thread_mdelay(execution_ms);

        /* Poll 阶段: 轮询直到芯片就绪或超时 */
        deadline = rt_tick_get() + rt_tick_from_millisecond(timeout_ms);

        while (!got_count)
        {
            /* 检查 Poll 阶段超时 */
            if (rt_tick_get() >= deadline)
            {
                rt_kprintf("协议层：轮询握手超时 (wait=%lu + poll=%lu ms)\n",
                           execution_ms, timeout_ms);
                return -RT_ETIMEOUT;
            }

            /* 发送传输标志 0x88 */
            ret = atsha_core_send_command();
            if (ret != RT_EOK)
            {
                rt_kprintf("协议层：发送传输标志(0x88)失败 (%d)\n", ret);
                return ret;
            }

            /*
             * 一次收完整帧 (Count+数据+CRC, 全程关中断):
             * 消除"先收 Count 再补收剩余"两次调用之间的采样空窗,
             * 芯片忙时首字节 edge-wait 快速超时返回, 由轮询重试兜底
             * 响应由分段接收改为整帧接收。
             */
            ret = hal_atsha_gpio_recv_frame(rx_raw, sizeof(rx_raw),
                                            timeout_ms, &frame_len);
            if (ret == RT_EOK)
            {
                count = (uint8_t)frame_len;
                got_count = RT_TRUE;
                break;
            }

            /* 芯片尚未就绪 — 等待后重试 */
            rt_thread_mdelay(1);
        }
    }

    /* 帧已完整收到 (含 Count), 直接解包 */
    size_t plen = *rx_len;
    ret = atsha_core_unpack(rx_raw, count, rx_buf, &plen, &status);

    if (ret != RT_EOK)
    {
        rt_kprintf("协议层：芯片返回错误或校验失败: %s (0x%02X)\n",
                   atsha_core_status_str(status), status);
        return ret;
    }

    *rx_len = plen;
    return RT_EOK;
}
