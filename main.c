/* ============================================================================
 *  main.c  -  RP2354 + OV5640 最小化 Camera Bring-up
 * ----------------------------------------------------------------------------
 *  本工程目的并 *不* 是完整接收图像，只是用最少的逻辑验证：
 *
 *      1. RP2354 是否正常运行                  -> PIO 心跳 + printf
 *      2. SCCB(I2C) 是否能配置 OV5640           -> 写寄存器返回值
 *      3. OV5640 是否输出 PCLK                  -> GPIO 边沿计数
 *      4. VSYNC / HREF 是否正常出现             -> GPIO 边沿计数
 *      5. 任意一个 DATA GPIO (D0) 是否跳变      -> GPIO 边沿计数
 *
 *  工程模块化：
 *      - heartbeat.pio       : PIO 单引脚心跳 (验证 RP2354 + PIO)
 *      - ov5640.c / ov5640.h : SCCB 驱动 + RGB565/DVP 配置 + color bar
 *      - main.c              : XCLK / GPIO / 主循环
 *
 *  约束 (按用户要求)：
 *      - 不使用 DMA
 *      - 不使用 FIFO
 *      - 不实现完整 RGB565 接收
 *      - 不验证 MPU6050 / IMU
 *      - PIO 仅占用 1 个引脚 (heartbeat) , 不参与图像采集
 * ============================================================================ */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

#include "ov5640.h"
#include "heartbeat.pio.h"   /* pico_generate_pio_header 产物 */

/* ---------------------------------------------------------------------------
 *  引脚分配 - 依据 docs/FPSP Camera.kicad_sch 实际网表
 * ---------------------------------------------------------------------------
 *  单端信号：
 *      XCLK    : GP26 (INT_XCLK), RP2354 -> R13 -> XCLK 网络 -> OV5640 主时钟 (PWM)
 *                注意: 相机时钟脚并不在 GP21 (GP21 是 DATA6 数据线)。
 *      VSYNC   : GP7  (VSYNC),  OV5640 -> RP2354, 帧同步
 *      HREF    : GP16 (HSYNC),  OV5640 -> RP2354, 行有效
 *      PCLK    : GP27 (PCLK),   OV5640 -> RP2354, 像素时钟
 *
 *  差分对信号（P/N 接收, 对应原理图 IMAGE_OUT_1 / IMAGE_OUT_2 = 数据位 D1/D0）：
 *      差分对1 (IMAGE_OUT_1): GP15(+) / GP12(-)
 *      差分对2 (IMAGE_OUT_2): GP14(+) / GP13(-)
 *      ※ 当前阶段差分发送端未启用, 这两对脚不会出现有效差分翻转。
 *
 *  其他：
 *      HEARTBEAT_PIN : GP5, 空闲脚。本板无 GPIO 用户 LED (D1~D4 为电源指示灯),
 *                      此脚仅作示波器 / 外接 LED 观测点, 不占用相机数据线。
 * --------------------------------------------------------------------------- */
#define CAM_PIN_XCLK         26u   /* INT_XCLK (PWM 输出) */
#define CAM_PIN_VSYNC         7u   /* 单端 VSYNC */
#define CAM_PIN_HREF         16u   /* 单端 HSYNC */
#define CAM_PIN_PCLK         27u   /* 单端 PCLK */

#define CAM_PIN_D0_P         15u   /* IMAGE_OUT_1 正极 */
#define CAM_PIN_D0_N         12u   /* IMAGE_OUT_1 负极 */
#define CAM_PIN_D1_P         14u   /* IMAGE_OUT_2 正极 */
#define CAM_PIN_D1_N         13u   /* IMAGE_OUT_2 负极 */

#define HEARTBEAT_PIN         5u   /* 空闲脚 (本板无板载 LED) */

/* ---------------------------------------------------------------------------
 *  GPIO 边沿采样参数
 *
 *  说明：
 *      OV5640 在 24 MHz XCLK + 标准 QVGA 输出时, VSYNC 是几十 Hz 量级、
 *      HREF 是若干 kHz, PCLK 是 MHz 量级。我们用裸 polling 不可能跟上 PCLK,
 *      但只要 PCLK 在跳, 多次采样必然能看见 0 -> 1 -> 0 翻转, 所以这里
 *      只要求 "在 N 次采样内出现 ≥ K 次边沿" 就判定为活跃。
 *
 *      SAMPLE_COUNT 取大一些以覆盖 VSYNC 完整帧周期。
 * --------------------------------------------------------------------------- */
#define SAMPLE_COUNT         120000u
#define MIN_EDGES_VSYNC          2u
#define MIN_EDGES_HREF           4u
#define MIN_EDGES_PCLK          50u
#define MIN_EDGES_D0             6u

/* ===========================================================================
 *  XCLK : PWM 输出 ~24 MHz 给 OV5640
 * ---------------------------------------------------------------------------
 *  公式：  f_pwm = clk_sys / ((TOP + 1) * div)
 *  默认 clk_sys = 150 MHz (RP2350 系列), TOP=1, div=3.125
 *      -> 150e6 / (2 * 3.125) = 24 MHz
 *
 *  即使 clk_sys 不是 150 MHz, 频率会有偏差, 但 OV5640 对 XCLK 的允许范围
 *  是 6 ~ 27 MHz, 不影响 Bring-up 验证。
 * =========================================================================== */
static void xclk_init_pwm_approx_24mhz(void) {
    gpio_set_function(CAM_PIN_XCLK, GPIO_FUNC_PWM);

    uint slice = pwm_gpio_to_slice_num(CAM_PIN_XCLK);
    uint chan  = pwm_gpio_to_channel(CAM_PIN_XCLK);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, 1u);
    pwm_config_set_clkdiv_int_frac(&cfg, 3u, 2u);   /* 3 + 2/16 = 3.125 */

    pwm_init(slice, &cfg, false);
    pwm_set_chan_level(slice, chan, 1u);            /* 占空比 50% */
    pwm_set_enabled(slice, true);
}

/* ===========================================================================
 *  Camera 信号脚初始化 - 支持单端+差分混合模式
 * ---------------------------------------------------------------------------
 *  所有脚均配置为输入，关闭上下拉，让 OV5640 自由驱动。
 *  差分脚对 (P/N) 独立读取，进行软件差分解码。
 * =========================================================================== */
static void camera_signal_gpio_init(void) {
    /* 单端信号脚 */
    const uint single_pins[] = {
        CAM_PIN_VSYNC,
        CAM_PIN_HREF,
        CAM_PIN_PCLK,
    };

    for (size_t i = 0; i < sizeof(single_pins) / sizeof(single_pins[0]); ++i) {
        gpio_init(single_pins[i]);
        gpio_set_dir(single_pins[i], GPIO_IN);
        gpio_disable_pulls(single_pins[i]);
    }

    /* 差分对脚 */
    const uint diff_pins[] = {
        CAM_PIN_D0_P, CAM_PIN_D0_N,
        CAM_PIN_D1_P, CAM_PIN_D1_N,
    };

    for (size_t i = 0; i < sizeof(diff_pins) / sizeof(diff_pins[0]); ++i) {
        gpio_init(diff_pins[i]);
        gpio_set_dir(diff_pins[i], GPIO_IN);
        gpio_disable_pulls(diff_pins[i]);
    }
}

/* ===========================================================================
 *  PIO 心跳: 仅用 1 个引脚驱动 LED, 用于肉眼判定 RP2354 在跑
 * =========================================================================== */
static void heartbeat_pio_start(void) {
    PIO  pio = pio0;
    uint sm  = 0;
    uint offset = pio_add_program(pio, &heartbeat_program);
    heartbeat_program_init(pio, sm, offset, HEARTBEAT_PIN);
}

/* ===========================================================================
 *  边沿检测：单端信号
 * ---------------------------------------------------------------------------
 *  通过密集 polling 在固定窗口内统计 0<->1 翻转次数。
 * =========================================================================== */
static bool detect_toggling(uint pin, uint32_t sample_count, uint32_t min_edges) {
    uint32_t edges = 0;
    bool     prev  = gpio_get(pin);

    for (uint32_t i = 0; i < sample_count; ++i) {
        bool now = gpio_get(pin);
        if (now != prev) {
            ++edges;
            prev = now;
            if (edges >= min_edges) {
                return true;   /* 提前结束, 缩短主循环延迟 */
            }
        }
    }
    return false;
}

/* ===========================================================================
 *  差分信号检测：P/N 对
 * ---------------------------------------------------------------------------
 *  差分信号逻辑：取 P & ~N，即正极高且负极低时为有效逻辑 1。
 *  检测差分对是否在切换。
 * =========================================================================== */
static bool detect_differential_toggling(uint pin_p, uint pin_n,
                                         uint32_t sample_count,
                                         uint32_t min_edges) {
    uint32_t edges = 0;
    bool prev = (gpio_get(pin_p) && !gpio_get(pin_n));

    for (uint32_t i = 0; i < sample_count; ++i) {
        bool now = (gpio_get(pin_p) && !gpio_get(pin_n));
        if (now != prev) {
            ++edges;
            prev = now;
            if (edges >= min_edges) {
                return true;
            }
        }
    }
    return false;
}

/* ===========================================================================
 *  main
 * =========================================================================== */
int main(void) {
    stdio_init_all();
    sleep_ms(1500);   /* 等待 UART 终端稳定 */

    printf("\n");
    printf("================================================\n");
    printf(" RP2354 + OV5640 minimal camera bring-up\n");
    printf("================================================\n");

    /* ----- 1) 先启动 PIO 心跳, 让 LED 立刻闪 -> RP2354 + PIO OK ----- */
    heartbeat_pio_start();
    printf("[boot] heartbeat PIO running on GP%u\n", HEARTBEAT_PIN);

    /* ----- 2) 给 OV5640 提供 XCLK ---------------------------------- */
    xclk_init_pwm_approx_24mhz();
    printf("[boot] XCLK PWM on GP%u (~24 MHz)\n", CAM_PIN_XCLK);

    /* ----- 3) 准备相机信号 GPIO 为输入 ----------------------------- */
    camera_signal_gpio_init();
    printf("[boot] camera signals initialized:\n");
    printf("       VSYNC=GP%u HREF=GP%u PCLK=GP%u (single-ended)\n",
           CAM_PIN_VSYNC, CAM_PIN_HREF, CAM_PIN_PCLK);
    printf("       D0_diff: GP%u(+)/GP%u(-), D1_diff: GP%u(+)/GP%u(-)\n",
           CAM_PIN_D0_P, CAM_PIN_D0_N, CAM_PIN_D1_P, CAM_PIN_D1_N);

    /* ----- 4) 初始化 SCCB 并跑上电时序 ----------------------------- */
    ov5640_i2c_init();
    ov5640_powerup_sequence();

    /* ----- 5) 读 OV5640 Chip ID 验证 SCCB 通路 --------------------- */
    ov5640_id_t id = {0};
    bool id_ok = ov5640_read_id(&id);
    if (!id_ok) {
        printf("[err ] SCCB read failed -- check wiring / pull-ups\n");
    } else {
        uint16_t chip_id = ((uint16_t)id.high << 8) | id.low;
        printf("[i2c ] OV5640 ID = 0x%04X\n", chip_id);
        if (id.high == 0x56u && id.low == 0x40u) {
            printf("camera detected\n");
        } else {
            printf("[warn] unexpected chip id\n");
        }
    }

    /* ----- 6) 配 RGB565/DVP + color bar ---------------------------- */
    if (!ov5640_init_rgb565_dvp()) {
        printf("[err ] RGB565/DVP configuration failed\n");
    } else {
        printf("[ok  ] OV5640 streaming on, color bar enabled\n");
    }

    /* ----- 7) 主循环: 周期性检测各信号是否仍在跳 -------------------- */
    bool printed_vsync = false;
    bool printed_href  = false;
    bool printed_pclk  = false;
    bool printed_data  = false;

    while (true) {
        if (!printed_pclk &&
            detect_toggling(CAM_PIN_PCLK, SAMPLE_COUNT, MIN_EDGES_PCLK)) {
            printf("pclk active\n");
            printed_pclk = true;
        }

        if (!printed_vsync &&
            detect_toggling(CAM_PIN_VSYNC, SAMPLE_COUNT, MIN_EDGES_VSYNC)) {
            printf("vsync detected\n");
            printed_vsync = true;
        }

        if (!printed_href &&
            detect_toggling(CAM_PIN_HREF, SAMPLE_COUNT, MIN_EDGES_HREF)) {
            printf("href detected\n");
            printed_href = true;
        }

        if (!printed_data &&
            detect_differential_toggling(CAM_PIN_D0_P, CAM_PIN_D0_N,
                                         SAMPLE_COUNT, MIN_EDGES_D0)) {
            printf("data toggling\n");
            printed_data = true;
        }

        /* 全部通过, 打印总结并复位标志, 周期性再验证一次 */
        if (printed_vsync && printed_href && printed_pclk && printed_data) {
            printf("------------------------------------------------\n");
            printf(" CAMERA BRING-UP CHECKS PASSED\n");
            printf("------------------------------------------------\n");
            printed_vsync = false;
            printed_href  = false;
            printed_pclk  = false;
            printed_data  = false;
            sleep_ms(2000);
        }

        sleep_ms(20);
    }
}
