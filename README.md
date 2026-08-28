# ATSHA204A RT-Thread Driver

面向 Microchip ATSHA204A 的独立 RT-Thread 底层驱动。公开边界止于 GPIO 单总线传输、命令封装和 SHA-256 工具，不包含产品状态机、业务数据布局、根密钥或客户配置。

## 功能

- GPIO 单总线唤醒、发送、接收与空闲控制
- ATSHA204A 常用底层命令与响应解析
- CRC/协议辅助逻辑
- 独立 SHA-256 实现

## 目录

```text
include/    驱动公开头文件
src/        协议、GPIO HAL 与 SHA-256 实现
SConscript  RT-Thread SCons 构建入口
```

## 集成

将仓库加入 RT-Thread BSP，并从父级 `SConscript` 调用本目录构建入口。板级工程需要提供 RT-Thread、`rtdevice.h`、`board.h` 和有效的 GPIO 配置。

应用层应在仓库外实现密钥装载、槽位策略、认证策略与数据编码。不要在源码或 Kconfig 中写入生产根密钥。

## 移植注意事项

- 单总线时序对中断延迟和 GPIO 切换开销敏感，换 MCU/主频后必须重新测量。
- 写入、锁定和 OTP 操作可能不可逆；建议先使用只读命令验证通信。
- ATSHA204A 的配置区和数据区布局由部署方决定，本仓库不假设任何产品槽位含义。

## 文档边界

内部设计决策、实物测试记录和产品集成说明保存在本地 `private-docs/`，该目录已被 `.gitignore` 排除，不会上传。

## 许可证

发布前需由代码权利人确认仓库级许可证。芯片名称和商标归其各自权利人所有。
