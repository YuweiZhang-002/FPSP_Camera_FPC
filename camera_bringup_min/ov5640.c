/* ============================================================================
 *  ov5640.c
 * ----------------------------------------------------------------------------
 *  OV5640 SCCB 最小驱动 - Bring-up 用
 *
 *  本文件刻意 *不* 实现完整 RGB565 接收 / DMA / FIFO / 帧缓存，
 *  只完成以下 4 件事：
 *      (1) i2c0 引脚 + 100 kHz 速率初始化
 *      (2) PWDN/RESETB 控制完成上电时序
 *      (3) 16-bit 地址寄存器读 / 写
 *      (4) 写入一个极简寄存器表，开启 RGB565 DVP 输出 + 内置 color bar
 *
 *  写入 color bar 的原因：
 *      OV5640 默认上电后传感器阵列输出未必稳定，若 AEC/AGC/ISP
 *      未配置，可能导致 D0/HREF 抖动不明显。color bar 是芯片内置
 *      的固定纹理发生器，能保证每行/每像素都有跳变，是 Bring-up
 *      最可靠的"信号源"。
 * ============================================================================ */

#include "ov5640.h"

#include <stddef.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

/* ---------------------------------------------------------------------------
 * 内部：8-bit value 的 16-bit-addr 寄存器项
 * --------------------------------------------------------------------------- */
typedef struct {
    uint16_t reg;
    uint8_t  value;
} ov5640_reg8_t;

/* 顺序写入一张寄存器表，遇到任意失败立刻返回 false */
static bool ov5640_write_list(const ov5640_reg8_t *regs, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (!ov5640_write_reg(regs[i].reg, regs[i].value)) {
            return false;
        }
    }
    return true;
}

/* ===========================================================================
 *  SCCB / I2C 初始化
 * =========================================================================== */
void ov5640_i2c_init(void) {
    i2c_init(OV5640_I2C_PORT, OV5640_I2C_BAUD_HZ);

    gpio_set_function(OV5640_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(OV5640_PIN_SCL, GPIO_FUNC_I2C);

    /* OV5640 模组板通常已经外置 4.7k 上拉，这里再启用 MCU 内部上拉
     * 作为冗余，避免飞线测试时无外部上拉导致挂死。 */
    gpio_pull_up(OV5640_PIN_SDA);
    gpio_pull_up(OV5640_PIN_SCL);
}

/* ===========================================================================
 *  上电时序
 * ---------------------------------------------------------------------------
 *  OV5640 推荐时序 (datasheet 2.7 节)：
 *      DOVDD / AVDD ramp up -> XCLK 稳定 -> PWDN 拉低 -> 等 1ms -> RESETB 拉高
 *
 *  本工程配置：
 *      - PWDN 硬件固定到 GND (10k 电阻)，无需软件控制
 *      - XCLK 由 main.c 在调用本函数之前先用 PWM 拉起来
 *      - 本函数仅控制 RESETB 完成上电时序
 * =========================================================================== */
void ov5640_powerup_sequence(void) {
    /* 只初始化 RESETB，PWDN 已硬件固定到 GND */
    gpio_init(OV5640_PIN_RESETB);
    gpio_set_dir(OV5640_PIN_RESETB, GPIO_OUT);

    /* 1) 进入复位 (RESETB 拉低) */
    gpio_put(OV5640_PIN_RESETB, 0);
    sleep_ms(5);

    /* 2) 释放复位，等 PLL/振荡器稳定 */
    gpio_put(OV5640_PIN_RESETB, 1);
    sleep_ms(20);
}

/* ===========================================================================
 *  SCCB 读写
 * ---------------------------------------------------------------------------
 *  OV5640 SCCB:
 *      - 7-bit 设备地址 (0x3C)
 *      - 16-bit 寄存器地址 (高字节先发)
 *      - 8-bit 数据
 * =========================================================================== */
bool ov5640_write_reg(uint16_t reg, uint8_t value) {
    uint8_t buf[3] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFFu),
        value,
    };

    return i2c_write_blocking(OV5640_I2C_PORT, OV5640_SCCB_ADDR,
                              buf, 3, false) == 3;
}

bool ov5640_read_reg(uint16_t reg, uint8_t *value) {
    if (value == NULL) {
        return false;
    }

    uint8_t addr[2] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFFu),
    };

    /* 先发 16-bit 地址 (重复起始, 不释放总线) */
    if (i2c_write_blocking(OV5640_I2C_PORT, OV5640_SCCB_ADDR,
                           addr, 2, true) != 2) {
        return false;
    }

    /* 再读 1 字节 */
    return i2c_read_blocking(OV5640_I2C_PORT, OV5640_SCCB_ADDR,
                             value, 1, false) == 1;
}

bool ov5640_read_id(ov5640_id_t *id) {
    if (id == NULL) {
        return false;
    }
    return ov5640_read_reg(OV5640_REG_CHIP_ID_HIGH, &id->high) &&
           ov5640_read_reg(OV5640_REG_CHIP_ID_LOW,  &id->low);
}

/* ===========================================================================
 *  RGB565 + DVP + color bar 最小化配置
 * =========================================================================== */
bool ov5640_init_rgb565_dvp(void) {
    /* --- 第一步：软复位 ----------------------------------------------------
     *   0x3103 = 0x11   : 系统输入时钟来自焊盘 (即外部 XCLK)
     *   0x3008 = 0x82   : bit7=1 软复位
     */
    static const ov5640_reg8_t reset_regs[] = {
        {0x3103u, 0x11u},
        {0x3008u, 0x82u},
    };
    if (!ov5640_write_list(reset_regs,
                           sizeof(reset_regs) / sizeof(reset_regs[0]))) {
        return false;
    }
    sleep_ms(10);

    /* --- 第二步：唤醒 + PLL + 输出格式 + 测试图样 ---------------------------
     *   0x3008=0x42  : 退出软复位, 但仍处于 software power-down (供后续配置)
     *   0x3035=0x21  : 系统时钟分频器, 保守值
     *   0x3036=0x46  : PLL multiplier
     *   0x4300=0x6F  : 输出格式 = RGB565
     *   0x501F=0x01  : ISP 输出选择 -> RGB
     *   0x3820=0x40  : 行/帧极性、镜像、翻转 (保持默认 + bit6 必置位)
     *   0x3821=0x06  : 同上，bit1 用于 binning
     *   0x503D=0x80  : 启用内置 color bar 测试图样 (关键!)
     *   0x3008=0x02  : 启动 streaming -> PCLK / HREF / VSYNC / D[0..7] 输出
     */
    static const ov5640_reg8_t dvp_rgb565_regs[] = {
        {0x3008u, 0x42u},
        {0x3035u, 0x21u},
        {0x3036u, 0x46u},
        {0x4300u, 0x6Fu},
        {0x501Fu, 0x01u},
        {0x3820u, 0x40u},
        {0x3821u, 0x06u},
        {0x503Du, 0x80u},
        {0x3008u, 0x02u},
    };
    if (!ov5640_write_list(dvp_rgb565_regs,
                           sizeof(dvp_rgb565_regs) / sizeof(dvp_rgb565_regs[0]))) {
        return false;
    }

    /* 留时间让传感器开始输出有效帧 */
    sleep_ms(20);
    return true;
}
