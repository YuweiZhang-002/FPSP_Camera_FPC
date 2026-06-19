# CMakeLists & VS Code 配置修复说明（方法 B：CMakeLists 单一事实来源）

修改日期：2026-05-26
项目目标硬件：**Pico 2（RP2350，Cortex-M33 / Arm Secure）**

本次修复采用「方法 B」：把目标芯片/板子的选择权固化在 `CMakeLists.txt` 里，让 Pi Pico VS Code 插件的状态栏选择无法静默覆盖编译目标，彻底消除「json 显示一套、CMake 实际编另一套」的不同步问题。

---

## 一、CMake 配置失败 `exited with code: 1` 的根因分析

CMake Tools 触发的命令（节选）：
```
cmake -DCMAKE_BUILD_TYPE=Debug
      -DCMAKE_C_COMPILER=.../arm-none-eabi-gcc.exe
      -DCMAKE_CXX_COMPILER=.../arm-none-eabi-g++.exe
      -DPython3_EXECUTABLE=d:/Python/python.exe
      -S D:/Cursor/New_Tests  -B d:/Cursor/New_Tests/build  -G Ninja
```
注意：**这条命令没有传 `-DPICO_PLATFORM` 也没有传 `-DPICO_BOARD`**。

### 直接证据：`build/CMakeCache.txt` 与 `build_rp2350/CMakeCache.txt` 对比

| 变量 | `build/`（失败的那个） | `build_rp2350/`（健康的） |
|---|---|---|
| `PICO_PLATFORM` | `rp2350`（未解析为具体子目标） | `rp2350-arm-s`（正确） |
| `PICO_BOARD` | `pico2` | `pico2` |
| `CMAKE_C_FLAGS` | **`-mcpu=cortex-m0plus -mthumb`** ← RP2040 标志 | `-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp -mcmse` |
| `CMAKE_CXX_FLAGS` | 同上，cortex-m0plus | 同上，cortex-m33 |
| `CMAKE_ASM_FLAGS` | 同上，cortex-m0plus | 同上，cortex-m33 |
| `CMAKE_BUILD_TYPE` | Debug | Release |

### 失败链路

1. `build/` 这个目录最早是在 `PICO_PLATFORM` 还是 RP2040（默认或被插件初始化为 RP2040）时第一次 configure 出来的，因此 CMake 把 `-mcpu=cortex-m0plus -mthumb` **写进了缓存**。
2. 后来 `CMakeLists.txt` 改成了 `set(PICO_PLATFORM rp2350)`，再次 configure 时：
   - `PICO_PLATFORM` 局部变量更新为 `rp2350`
   - 但缓存里的 `CMAKE_C_FLAGS / CMAKE_CXX_FLAGS / CMAKE_ASM_FLAGS` 是 **CMake 顶级缓存条目，不会因为 `PICO_PLATFORM` 改变而自动重置**（Pico SDK 只在「第一次」根据平台决定这些 flags）
3. CMake 用旧的 cortex-m0plus 标志去编 RP2350 的 SDK 源码 → 编译期断言 / 头文件条件编译失败 / `try_compile` 不通过 → configure 阶段直接退出 `1`。
4. 又因为 `CMake Tools` 子进程**不继承** `settings.json → terminal.integrated.env.windows` 里的 `PICO_SDK_PATH / PICO_TOOLCHAIN_PATH`，依赖这些环境变量的辅助逻辑也可能再绊一脚（但本次主因是上面的 flags 失配）。

### 修复

- **必做**：删除 `build/` 目录（缓存已损坏，无法自愈），让下一次 configure 从零生成。
- **配套**：本次把 `CMakeLists.txt` 里的 `set(PICO_PLATFORM …)` 改成 `CACHE STRING … FORCE`，并放在 `pico_sdk_import.cmake` **之前**，这样以后即使再次出现缓存值漂移，每次 configure 都会被强制刷回正确值。

> 顺手把 `build_pico2/` 也删了；它和 `build_rp2350/` 一样是历史残留，统一构建目录之后不需要保留。如果想留一份能用的 uf2 当备份，先把 `build_rp2350/new_tests_app.uf2` 复制到别处再删。

---

## 二、本次修改的文件清单

### 1. `CMakeLists.txt`
**变更内容**：把 `set(PICO_PLATFORM ...)` 和 `set(PICO_BOARD ...)` 提前到 `include(pico_sdk_import.cmake)` 之前，并改成 `CACHE STRING ... FORCE`。

```cmake
# === Single source of truth for target chip / board (Plan B) ===
# Hardcoded BEFORE pico_sdk_import so the SDK's toolchain selection picks the
# correct cortex-m33 / armv8-m.main flags on first configure.
# CACHE ... FORCE re-asserts the value on every configure, so a stale cache
# entry (e.g. from a previous rp2040 build) cannot win.
# Do NOT use `if(NOT DEFINED ...)` guards here: a stale CACHE value counts as
# defined and would silently keep the wrong target.
set(PICO_PLATFORM rp2350 CACHE STRING "Pico Platform" FORCE)
set(PICO_BOARD    pico2  CACHE STRING "Pico Board"    FORCE)

include(pico_sdk_import.cmake)

project(new_tests C CXX ASM)
```

为什么这样写：
- 必须在 `include(pico_sdk_import.cmake)` 和 `project()` **之前**——SDK 是在 `project()` 触发的工具链初始化阶段读 `PICO_PLATFORM` 来决定 `-mcpu` 等编译参数的，晚于这个时刻设值就来不及。
- 用 `CACHE ... FORCE` 而不是简单 `set(PICO_PLATFORM rp2350)`：FORCE 每次 configure 都会把缓存条目改写成我们要的值，不会被「上一次 configure 把脏值写进 CMakeCache」长期污染。
- **明确不加 `if(NOT DEFINED PICO_PLATFORM)` 守卫**：缓存里有值就算 "defined"，守卫会让脏缓存继续生效——和我们 plan B 的初衷相反。

### 2. `.vscode/settings.json`
**变更内容**：新增一行
```json
"cmake.buildDirectory": "${workspaceFolder}/build",
```
原本没有显式设置 `cmake.buildDirectory`，CMake Tools 走默认 `${workspaceFolder}/build`，但 `tasks.json` / `launch.json` 都指向 `build_rp2350`，IntelliSense (`c_cpp_properties.json`) 又指向 `build` —— 形成三套构建目录互相错位。显式设置以后所有工具都从同一个目录读写。

### 3. `.vscode/tasks.json`
**变更内容**：
- 把所有 `build_rp2350` 替换为 `build`（统一构建目录）
- `Run Project` 任务的主 `command` 字段把 `${env:HOME}` 改成 `${userHome}`（Windows 下 `HOME` 通常未定义，会让 picotool 启动直接失败；虽然 `windows` 字段会兜底，但主字段是无效占位符就不应保留）
- `Flash` 任务里 `target/${command:raspberry-pi-pico.getTarget}.cfg` 替换为字面量 `target/rp2350.cfg`
- `Rescue Reset` 任务里 `target/${command:raspberry-pi-pico.getChip}-rescue.cfg` 替换为字面量 `target/rp2350-rescue.cfg`
- 删掉了 `Flash` / `Rescue Reset` 中尾随的多余逗号（JSON 严格语法虽然 VS Code 容忍，但有些 CI 工具会报错）

为什么把 `${command:...}` 改字面量：这些命令本质上读插件状态栏的当前选择，正好就是「json 和 cmake 不同步」的另一半根源。Plan B 既然钉死了硬件，这里也跟着钉死。

### 4. `.vscode/launch.json`
**变更内容**：
- 所有 `build_rp2350` → `build`
- `${command:raspberry-pi-pico.getGDBPath}` → `${userHome}/.pico-sdk/toolchain/14_2_Rel1/bin/arm-none-eabi-gdb.exe`
- `${command:raspberry-pi-pico.getChipUppercase}` → `RP2350`
- `${command:raspberry-pi-pico.getTarget}` → `rp2350`
- `${command:raspberry-pi-pico.getChip}` → `rp2350`（SVD 路径里）

理由同 `tasks.json`：调试器目标 / SVD 文件 / GDB 路径都钉死在 RP2350 Arm 工具链。

### 5. `.vscode/c_cpp_properties.json`
**变更内容**：`cppStandard` 从 `c++14` 改为 `c++17`，与 `CMakeLists.txt` 里 `set(CMAKE_CXX_STANDARD 17)` 一致。`compileCommands` 和 `forcedInclude` 已经指向 `${workspaceFolder}/build/...`，统一构建目录之后无需再改。

### 6. 未改动但要留意的文件
- `.vscode/cmake-kits.json`：仍使用 `${command:raspberry-pi-pico.getCompilerPath}` 等。当前 toolchain 目录是 `14_2_Rel1`（ARM 工具链），插件命令返回的就是 `arm-none-eabi-gcc`，与 Plan B 一致，因此**不动**。如果未来想完全脱离插件，可再把 `cmake-kits.json` 里的 `${command:...}` 也替换为字面路径。
- `pico_sdk_import.cmake`：SDK 标准模板，不动。

---

## 三、修复后必须执行的操作

> 这一步是为了清掉那个污染的 build 缓存。**直到你做完这步，CMake configure 仍然会失败。**

PowerShell：
```powershell
Remove-Item -Recurse -Force D:\Cursor\New_Tests\build
Remove-Item -Recurse -Force D:\Cursor\New_Tests\build_pico2
# build_rp2350 可选：如果你想保留上次的 uf2，先复制再删
Remove-Item -Recurse -Force D:\Cursor\New_Tests\build_rp2350
```

然后在 VS Code 里：
1. `Ctrl+Shift+P` → **CMake: Delete Cache and Reconfigure** （触发一次干净的 configure）
2. 或者直接重新跑 `Compile Project` 任务

预期：configure 通过，生成 `D:/Cursor/New_Tests/build/new_tests_app.{elf,uf2,bin,hex,dis,map}`，且 `build/CMakeCache.txt` 里 `PICO_PLATFORM=rp2350-arm-s`、`CMAKE_C_FLAGS` 含 `cortex-m33`。

---

## 四、回滚指引

如果将来想换回「跟随插件」模式（方法 A），步骤反过来即可：
1. `CMakeLists.txt`：删除 `set(PICO_PLATFORM ... FORCE)` 和 `set(PICO_BOARD ... FORCE)` 两行
2. `.vscode/tasks.json` / `launch.json`：把 `rp2350` / `RP2350` / 显式 GDB 路径恢复成 `${command:raspberry-pi-pico.getTarget}` / `${command:raspberry-pi-pico.getChipUppercase}` / `${command:raspberry-pi-pico.getGDBPath}`
3. `.vscode/settings.json`：可选删除 `cmake.buildDirectory`（让插件管）
4. 删 `build/`，重新 configure

---

## 五、Run Project 报 `No accessible RP-series devices in BOOTSEL mode were found` 的解决

这个错和 CMake 配置无关，是 picotool 找不到设备。最稳的恢复路径：

1. **按住板上的 BOOTSEL 键，插 USB，松开**——设备直接进入 BOOTSEL 模式
2. 用 Zadig 把 `RP2 Boot` 接口装成 WinUSB 驱动（只需要做一次，之后系统都认）
3. 再次执行 `Run Project` 任务

之后只要你刷入的固件里启用了 `pico_enable_stdio_usb(... 1)`（本项目已启用），picotool 就能通过 USB 重启接口自动把设备切到 BOOTSEL，下次起不再需要手按按键。
