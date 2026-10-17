# GD32F303CBT6 OLED 小恐龙

给 4T8003 双节点板 **B 节点**制作的单色 OLED 小游戏。主控为 GD32F303CBT6，屏幕为 128×32。工程使用 Keil MDK，游戏代码在 `projects/GD32F303CB/main.c`。

## 玩法

| 板上按键 | GD32 引脚 | 功能 |
| --- | --- | --- |
| BSW1 | PC15 | 暂停 / 继续；暂停时显示双竖条图标 |
| BSW2 | PC14 | 按住蹲下；空中按下会快速落地 |
| BSW3 | PC13 | 重新开始 |
| BSW4 | PB4 | 跳跃 |

跳过仙人掌，蹲下避开低飞的鸟。障碍物速度随通过数量增加，上限为每游戏帧 7 像素。跳起来碰到金币可得分：连续吃到时依次得 **1、2、4、8 分**；漏掉一枚金币后倍率重置为 1。左上角显示下一枚金币的倍率，右上角显示总分。

OLED 从正常显示开始，每 8 秒自动在正常显示和黑夜反色模式之间切换。

## 硬件连接

本工程针对 B 节点：PB5 控制 OLED2 电源（低电平开启），PB6 为软件 I²C 时钟，PB7 为数据线。OLED 使用 SSD1306 兼容的 128×32 显存格式，I²C 7 位地址为 `0x3C`。PB4 原为 JTAG 引脚，程序关闭 JTAG 并保留 SWD，供 SW4 使用。

引脚对应依据用户提供的 `SCH_4T8003_GD32_2022-08-23.pdf` 原理图。该 PDF 未包含在仓库中。

## 准备开发环境

1. 安装 Keil MDK 和 `GigaDevice.GD32F30x_DFP` 设备包。本工程使用过 DFP 2.6.0、ARM Compiler 5.06。
2. 从兆易创新官网下载 **GD32F30x Firmware Library V3.0.3**，解压后放在仓库根目录的 `sdk/GD32F30x_Firmware_Library_V3.0.3/`。目录下应能找到 `Firmware/CMSIS` 和 `Firmware/GD32F30x_standard_peripheral`。固件库由原厂提供，未随此仓库再分发。
3. 在 Keil 打开 `projects/GD32F303CB/GD32F303CB.uvprojx`，按 **F7** 编译。Keil 生成 `projects/GD32F303CB/output/Project.hex`。
4. 连接 B 节点的 SWD，在 Keil 按 **F8** 烧录。若只需体验当前版本，也可直接使用 `firmware/GD32F303CB-Dino.hex`。

**注意：** `projects/GD32F303CB/build-keil.ps1` 是另一套命令行构建方式，输出位于 `build/`。Keil 的 F8 使用 `output/Project.axf`；修改代码后请先在 Keil 按 F7，再按 F8，以免烧录旧版本。

## 电脑预览

电脑上安装 Python 3（包含 Tkinter）后运行：

```powershell
python projects/GD32F303CB/tools/dino_pc_preview.py
```

预览键位：空格或上方向键跳跃、下方向键蹲下、`R` 重来、`P` 暂停。预览采用与固件相同的游戏规则，实际屏幕刷新速度仍以板上表现为准。

## 文件说明

- `projects/GD32F303CB/main.c`：游戏逻辑、按键与 OLED 驱动；只刷新变化的 OLED 区域。
- `projects/GD32F303CB/GD32F303CB.uvprojx`：Keil 工程。
- `projects/GD32F303CB/tools/dino_pc_preview.py`：电脑预览。
- `projects/GD32F303CB/tools/image_to_oled.py`、`gif_to_oled.py`：之前图片和 GIF 的 OLED 编码工具。
- `projects/GD32F303CB/assets/`、`oled_image.h`、`oled_animation.h`：之前的静态图片和动画实验素材；当前游戏固件不引用这些头文件。
- `firmware/GD32F303CB-Dino.hex`：本次打包时已编译的游戏固件。

## 构建验证

在 Keil MDK 5.26 / ARM Compiler 5.06 下编译 `GD32F303CB` 目标：**0 errors，0 warnings**。固件的实际手感、OLED 时序和按键响应需在目标板上验证。
