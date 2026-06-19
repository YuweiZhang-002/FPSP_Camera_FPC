# camera_bringup_min

这是一个面向 RP2354 + OV5640 的最小化 Camera Bring-up 工程，目标不是完整图像接收，而是验证以下信号链是否工作：

- RP2354 是否正常运行
- SCCB(I2C) 是否可以成功配置 OV5640
- OV5640 是否输出 PCLK
- VSYNC / HREF 是否出现跳变
- D0 是否存在数据跳变

## 工程特点

- 使用 Pico SDK
- 不使用 DMA
- 不使用 FIFO
- 不实现完整 RGB565 图像接收
- 不依赖 Linux / OpenCV / V4L2
- 通过 `printf` 输出 bring-up 结果

## 目录说明

- `main.c`：主程序，负责 XCLK、GPIO 输入检测和状态打印
- `ov5640.c` / `ov5640.h`：OV5640 的 SCCB(I2C) 初始化、上电时序、ID 读取、最小 RGB565/DVP 配置
- `heartbeat.pio`：PIO 单引脚心跳，用于验证 RP2354 + PIO 子系统正常运行
- `CMakeLists.txt`：Pico SDK 构建入口

## 正确的运行方式

这个工程不是直接在 bash 里执行一个 `.exe`。Pico SDK 工程通常要先编译，再把生成的 `.uf2` 烧录到开发板。

推荐流程如下：

```bash
cmake -S . -B build
cmake --build build
```

编译完成后，会在 `build/` 下生成 `.uf2` / `.elf` 等文件。把 `.uf2` 拖到 RP2354 的 USB 盘里即可运行。

## 常见错误

如果你在终端里看到类似下面的报错：

```text
bash: enter program name, for example D:\Cursor\project\pico-sdk-master/a.exe: No such file or directory
```

这表示你把 VS Code / 终端里的占位提示当成了真实命令。它不是程序名，也不是可执行文件路径。

原因通常是：

- 还没有完成编译
- 运行配置里的 program 路径为空
- 把提示文本原样输入到了终端

## 默认引脚

引脚已依据 `docs/FPSP Camera.kicad_sch` 实际网表对齐。如果板级连线不同，请修改 `main.c` 和 `ov5640.h` 中的宏定义。

- XCLK: `GPIO26`（INT_XCLK，经 R13 接到相机 XCLK；GP21 实为 DATA6 数据线）
- VSYNC: `GPIO7`
- HREF: `GPIO16`（HSYNC）
- PCLK: `GPIO27`
- D0 差分（IMAGE_OUT_1）正极: `GPIO15` / 负极: `GPIO12`
- D1 差分（IMAGE_OUT_2）正极: `GPIO14` / 负极: `GPIO13`
- I2C SDA: `GPIO10`
- I2C SCL: `GPIO9`
- RESETB: `GPIO8`
- PWDN: 未接到 RP2354（硬件固定到 GND），软件不控制
- 心跳/指示: `GPIO5`（空闲脚；本板无 GPIO 用户 LED，D1~D4 为电源指示灯）

## 串口输出

工程默认启用 UART stdio。上电后可看到类似输出：

- `camera detected`
- `pclk active`
- `href detected`
- `vsync detected`
- `data toggling`

## 备注

当前实现使用了内置 color bar 测试图样，目的是让 DVP 输出更容易被观察和验证。如果你希望改成真实图像输出，可以再把测试图样关闭。
