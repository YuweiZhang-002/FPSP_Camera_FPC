/* ============================================================================
 *  ov5640.h
 * ----------------------------------------------------------------------------
 *  OV5640 SCCB (I2C 兼容) 最小化驱动头文件
 *
 *  功能范围 (仅用于 Bring-up 验证)：
 *      - 初始化 I2C(SCCB) 接口
 *      - 控制 PWDN / RESETB 完成上电时序
 *      - 16-bit 寄存器读 / 写
 *      - 读 Chip ID (期望 0x5640)
 *      - 配置 OV5640 输出 RGB565 DVP, 并开启 color bar 测试图样，
 *        使 VSYNC / HREF / PCLK / D0 必然有跳变，便于裸机验证
 * ============================================================================ */

#ifndef CAMERA_BRINGUP_OV5640_H
#define CAMERA_BRINGUP_OV5640_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * SCCB 走线配置 - 与开发板硬件实际接线对齐
 * --------------------------------------------------------------------------- */
#define OV5640_I2C_PORT        i2c0
#define OV5640_I2C_BAUD_HZ     100000u   /* 100 kHz，SCCB 兼容速率 */
#define OV5640_SCCB_ADDR       0x3Cu     /* 7-bit 写地址 0x78>>1 */
#define OV5640_PIN_SDA         10u       /* I2C SDA -> GPIO10 */
#define OV5640_PIN_SCL          9u       /* I2C SCL -> GPIO9 */

/* ---------------------------------------------------------------------------
 * 电源/复位控制脚
 * --------------------------------------------------------------------------- */
/* PWDN: 原理图中未连到 RP2354 任何 GPIO (硬件固定到 GND), 软件不控制。
 * 原值 14 会与差分对 IMAGE_OUT_2(+) = GP14 冲突, 故移除该宏定义以免误用。 */
/* #define OV5640_PIN_PWDN     14u */
#define OV5640_PIN_RESETB       8u       /* RESETB -> GPIO8 (~RESET), 低电平进入硬复位 */

/* ---------------------------------------------------------------------------
 * 关键寄存器
 * --------------------------------------------------------------------------- */
#define OV5640_REG_CHIP_ID_HIGH 0x300Au  /* 期望 0x56 */
#define OV5640_REG_CHIP_ID_LOW  0x300Bu  /* 期望 0x40 */

/* OV5640 ID 读取结果 */
typedef struct {
    uint8_t high;   /* 0x300A 内容 */
    uint8_t low;    /* 0x300B 内容 */
} ov5640_id_t;

/* ---------------------------------------------------------------------------
 *  API
 * --------------------------------------------------------------------------- */

/**
 * @brief 初始化 SCCB(I2C) 接口与 SDA/SCL 引脚
 */
void ov5640_i2c_init(void);

/**
 * @brief 控制 PWDN / RESETB，完成 OV5640 上电时序
 *
 * 时序参考 OV5640 datasheet：先 PWDN=1+RESETB=0，然后 PWDN=0，
 * 延时后释放 RESETB=1，等待内部 PLL/振荡器稳定。
 */
void ov5640_powerup_sequence(void);

/**
 * @brief 写 8-bit 数据到 16-bit 地址寄存器
 * @return true 成功 / false 失败
 */
bool ov5640_write_reg(uint16_t reg, uint8_t value);

/**
 * @brief 从 16-bit 地址寄存器读 8-bit 数据
 * @return true 成功 / false 失败
 */
bool ov5640_read_reg(uint16_t reg, uint8_t *value);

/**
 * @brief 读取 Chip ID 高低字节
 * @return true SCCB 读取成功; false 通信失败
 */
bool ov5640_read_id(ov5640_id_t *id);

/**
 * @brief 配置 OV5640 输出 RGB565 / DVP 并开启 color bar 测试图样
 *
 * 这是一个 *极简* 的配置序列，目的不是获得完美图像，而是让
 * VSYNC / HREF / PCLK / D[0:7] 必然有持续可观察的跳变，便于
 * 主程序通过 GPIO 边沿计数确认 OV5640 正在输出。
 *
 * @return true SCCB 全部写入成功; false 中途出错
 */
bool ov5640_init_rgb565_dvp(void);

#ifdef __cplusplus
}
#endif

#endif  /* CAMERA_BRINGUP_OV5640_H */
