# 🎮 坦克大战（1990 经典版）

> UEFI Shell 原生图形游戏 | 基于 EDK2 + GOP 协议 | QEMU/OVMF 运行

[![Version](https://img.shields.io/badge/version-0.6-blue)](https://github.com)
[![Platform](https://img.shields.io/badge/platform-UEFI%20Shell-brightgreen)](https://github.com)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

[**📖 English Version**](README_EN.md)


## 📖 产品概述

坦克大战（1990 经典版）是一款在 **UEFI Shell** 环境下运行的俯视角固定地图坦克射击游戏，复刻了 1990 年红白机经典《坦克大战》（Battle City）的核心玩法。

游戏采用 UEFI **GOP（Graphics Output Protocol）** 进行纯软件图形渲染，在 QEMU + OVMF 虚拟机环境下运行，亦可部署到真实 UEFI 固件中。
<img width="624" height="469" alt="short1" src="https://github.com/user-attachments/assets/11b53fe5-207c-45f7-a23a-146b3cfddfc8" />

<img width="640" height="505" alt="Snipaste_2026-04-25_16-06-12" src="https://github.com/user-attachments/assets/7a9ee05c-627a-4f64-b878-ab43fa42eae0" />
<img width="640" height="505" alt="Snipaste_2026-04-25_16-06-36" src="https://github.com/user-attachments/assets/003bc412-f703-43e1-a599-a132e183b8bd" />

### 核心玩法

玩家操控一辆**金色坦克**在砖墙和钢板构成的战场上移动，发射炮弹消灭从地图顶部不断刷新的敌方坦克，保护位于地图底部中央的己方基地（鹰徽）。拾取战场上的道具可以升级武器、增加生命、获得无敌护盾或冻结/清除敌军。

| 特性 | 说明 |
|------|------|
| 关卡数 | **35 关**，8 张地图循环 |
| 敌坦类型 | **4 种**（基础、快速、重型、速射） |
| 道具种类 | **5 种**（升级、加命、护盾、冻结、炸弹） |
| 帧率 | 30 FPS |
| 分辨率 | 640×480 BGRA |
| 语言 | 简体中文（91 字位图字库） |

---

## 💻 系统要求

| 项目 | 要求 |
|------|------|
| 虚拟化环境 | QEMU 9.x+ (x86_64) |
| UEFI 固件 | OVMF (`edk2-x86_64-code.fd`) |
| 内存 | 最低 256 MB |
| 显示 | 支持 640×480 GOP 模式（兼容其他分辨率） |
| 输入 | 键盘（UEFI SimpleTextIn 协议） |
| 主机系统 | Windows 10/11、Linux（QEMU 支持的任何平台） |

### 开发环境（如需自行编译）

| 工具 | 版本要求 |
|------|----------|
| Visual Studio | 2019 Professional（含 MSVC 14.29） |
| EDK2 | [Tianocore 官方仓库](https://github.com/tianocore/edk2) |
| Python | 3.10+ |
| QEMU | 9.x（自带 EDK2 固件） |

---

## 🚀 快速开始

### 直接运行（预编译 EFI）

```bash
# 1. 准备文件
mkdir -p qemu/disk
cp Tank.efi qemu/disk/

# 创建自动启动脚本
cat > qemu/disk/startup.nsh << 'EOF'
echo "=== Starting Tank Battle ==="
Tank.efi
EOF

# 2. 复制 QEMU 自带 EDK2 固件
cp "C:/Program Files/qemu/share/edk2-x86_64-code.fd" qemu/OVMF_CODE.fd
cp "C:/Program Files/qemu/share/edk2-i386-vars.fd" qemu/OVMF_VARS.fd

# 3. 启动游戏
qemu-system-x86_64 \
  -drive if=pflash,format=raw,file=qemu/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,file=qemu/OVMF_VARS.fd \
  -drive format=raw,file=fat:rw:qemu/disk \
  -net none -m 256
```

UEFI Shell 启动后会自动执行 `startup.nsh` 运行游戏。

### 从源码编译

```bash
# 1. 将本仓库放入 EDK2
cp -r Application/Tank /path/to/edk2/EmulatorPkg/Application/Tank

# 2. 在 EmulatorPkg/EmulatorPkg.dsc 的 [Components] 节添加：
echo "  EmulatorPkg/Application/Tank/Tank.inf" >> EmulatorPkg/EmulatorPkg.dsc

# 3. 在 EmulatorPkg/EmulatorPkg.fdf 的 [FV.FvRecovery] 节添加：
echo "  INF EmulatorPkg/Application/Tank/Tank.inf" >> EmulatorPkg/EmulatorPkg.fdf

# 4. 编译（Windows + VS2019）
cd /path/to/edk2
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
call edksetup.bat
build -p EmulatorPkg\EmulatorPkg.dsc -a X64 -t VS2019 -b DEBUG -m EmulatorPkg/Application/Tank/Tank.inf
```

编译产物 `Tank.efi` 位于 `Build/EmulatorX64/DEBUG_VS2019/X64/Tank.efi`。

---

## 🎮 游戏操作

| 按键 | 功能 |
|------|------|
| **方向键 ↑ ↓ ← →** | 控制坦克移动 |
| **空格键** | 发射炮弹 / 菜单确认 |
| **回车键** | 菜单确认 |
| **ESC** | 退出游戏 |

### 移动规则

- 🧱 **砖墙**：可被炮弹摧毁，产生碎片粒子
- 🛡️ **钢板**：普通弹无法摧毁，需穿甲弹
- 🌊 **水域**：坦克不可通过，炮弹可通过
- 🌲 **树林**：隐藏坦克，炮弹可穿过
- 🦅 **基地**：不可通过，被摧毁即游戏结束

---

## 🎯 游戏系统

### 地图系统（8 张）

游戏为 26×26 瓦片网格（416×416 像素），每关循环使用不同地图：

| # | 名称 | 核心特点 |
|---|------|----------|
| 1 | 经典布局 | 对称砖墙 + 钢板核心 + 水域障碍 |
| 2 | 堡垒 | 四角钢板塔楼 + 环形砖墙 + 树林掩体 |
| 3 | 迷宫 | 走廊式砖墙 + 钢板闸门 + 多水域分割 |
| 4 | 竞技场 | 中心广场 + 四角钢墙 + 散射树丛 |
| 5 | 狭路 | 钢墙夹道 + 连续隘口 + 树林节点 |
| 6 | 堡垒II | 四方钢堡 + 中心水域 + 双层砖阵 |
| 7 | 渡河 | 大河穿越 + 桥头攻防 + 双侧砖垒 |
| 8 | 十字火 | 中央十字通道 + 双侧水域 + 对角砖阵 |

### 关卡与波次

- 总计 **35 关**，8 张地图循环
- 每关敌军数量 = 关卡数 × 2 + 4（最多 20）
- 敌坦克从地图顶部 3 个刷新点依次出现
- 场上同时最多 4 辆敌坦克
- 全部敌军消灭后自动通关，等待 2 秒进入下一关

### 敌方坦克类型

| 类型 | 名称 | 颜色 | 体型 | 速度 | 子弹速度 | 特点 |
|:---:|------|------|------|------|----------|------|
| 0 | **基础型** | 银色 | 标准 | 15 px/s | 常规 | 综合均衡 |
| 1 | **快速型** | 红色 | 缩小30% | **60 px/s** | 常规 | 加长炮管、白色速度条纹、4×速度 |
| 2 | **重型** | 藏蓝 | 标准 | 15 px/s | 常规 | 装甲外观 |
| 3 | **速射型** | 绿色 | 标准 | 15 px/s | **高速弹** | 炮塔黄色标识、子弹 1.6× |

### 道具系统

消灭敌方坦克有 **50% 概率**掉落道具（持续约 10 秒，消失前 2 秒闪烁）：

| 道具 | 图标 | 效果 |
|:---:|------|------|
| ⭐ **星** | 金色光球 | **武器升级**：炮弹速度↑ → 穿甲弹（可摧毁钢板） |
| 💚 **命** | 绿色光球 | **加命**：生命 +1（最高 9 条） |
| 💙 **护** | 蓝色光球 | **无敌护盾**：蓝色脉冲光环，持续约 5 秒 |
| 🤍 **停** | 白色光球 | **冻结**：敌方坦克停止移动 |
| ❤️ **炸** | 红色光球 | **清屏**：立即消灭全部敌军并触发过关 |

### 生命与游戏结束

- 初始 **3 条命**，每通关奖励 +1（最多 9 条）
- 被击毁后 2 秒重生无敌时间
- 基地被毁或生命耗尽 → 游戏结束
- 35 关全部通过 → 胜利

---

## 🖥️ 界面说明

### 标题画面

- **左侧**：满屏战场预览（深灰网格背景），中央「坦克大战」金色 2× 标题，三辆示范坦克排列，底部绿色闪烁「按空格开始」
- **右侧**：信息面板——版本号（0.6）、操作说明、道具说明（图标+双列对齐）、敌军类型介绍

### 游戏内 HUD（右侧面板）

| 区域 | 内容 |
|------|------|
| 关卡 | 7 段数码管显示（支持两位数 1-35） |
| 生命 | 金色坦克图标 × 数量 |
| 敌军 | 银色坦克图标 × 剩余数 |
| 得分 | 金色进度条 |
| 道具状态 | 升级 / 无敌状态显示 |
| 版本 | 底部「坦克大战 0.6」 |

---

## ✨ 视觉特效

| 特效 | 描述 |
|------|------|
| 🔫 **炮弹** | 圆形白色弹头 + 黄色内核 + 橙色拖尾 |
| 💥 **爆炸** | 橙色外圈 → 黄色中层 → 白色核心，逐帧收缩 |
| 🧱 **砖墙碎片** | 棕色粒子随机弹射 + 重力下落 + 渐隐 |
| 🛡️ **无敌光环** | 8 方向脉冲钻石光点，蓝色→橙色警告 |
| 🌐 **道具光球** | 彩色辉光外环 + 黑色间隔 + 白色高光 |
| ✨ **出生粒子** | 坦克出现时喷涌彩色粒子 |
| 💫 **残骸粒子** | 击中/消灭时喷出对应颜色碎片 |

---

## 🔧 技术架构

### 图形渲染

| 项目 | 实现 |
|------|------|
| 协议 | UEFI GOP（Graphics Output Protocol） |
| 模式 | 640×480 BGRA，`PixelBlueGreenRedReserved8BitPerColor` |
| 缓冲 | 内存双缓冲（1.2 MB），每帧一次性 `Blt` 传输 |
| 帧率 | 30 FPS（`gBS->Stall(33333)`） |
| 字体 | 91 字中文位图字库（16×16 SimSun 宋体）+ 7 段数码管 |

### 源文件结构

```
EmulatorPkg/Application/Tank/
├── Tank.inf          # EDK2 模块定义
├── config.h          # 游戏常量与数据结构
├── sprite.h          # 瓦片/道具 BGRA 位图数据
├── font.h            # 中文字模（91 字 16×16）
└── main.c            # 主程序（约 1500 行）
    ├── GOP 初始化与模式设置
    ├── 输入轮询（非阻塞 ReadKeyStroke）
    ├── 游戏状态机（5 状态）
    ├── 地图系统（8 张 26×26 瓦片 + 碰撞检测）
    ├── 实体管理（坦克、炮弹、道具、爆炸、碎片）
    ├── 敌方 AI（随机移动 + 开火 + 类型差异）
    ├── 粒子系统（生成、运动、重力、消隐）
    ├── 中文字体渲染（16px 字形 + 7 段数码管）
    └── 渲染器（瓦片、精灵、HUD、覆盖层）
```

### 依赖的 UEFI 协议与库

| 协议/库 | 用途 |
|----------|------|
| `Protocol/GraphicsOutput.h` | 图形输出、模式设置、Blt 传输 |
| `Protocol/SimpleTextIn.h` | 键盘输入轮询 |
| `Library/UefiLib.h` | Print 调试输出 |
| `Library/BaseMemoryLib.h` | SetMem/CopyMem |
| `Library/MemoryAllocationLib.h` | 帧缓冲 AllocatePool |
| `Library/UefiBootServicesTableLib.h` | gBS（LocateProtocol, Stall） |
| `Library/UefiRuntimeServicesTableLib.h` | gST（ConIn 输入） |

### 关键技术约束

- **无浮点运算**：全部整数计算，三角函数简化为矩形绘制
- **无标准 C 库**：不使用 `<stdio.h>`、`<stdlib.h>`、`<math.h>`
- **静态分配优先**：实体数组编译时固定大小，避免运行时碎片
- **UTF-8 with BOM**：源文件编码要求

---

## 📋 版本历史

| 版本 | 日期 | 主要变更 |
|:---:|------|------|
| **0.1** | 2026-04-25 | 初始原型：GOP 渲染、矩形坦克、单一地图 |
| **0.2** | 2026-04-25 | 像素坐标统一、BGRA 颜色修正、56 字中文位图字模 |
| **0.3** | 2026-04-25 | 精细坦克精灵（履带/负重轮/炮塔/面板）、道具系统 |
| **0.4** | 2026-04-25 | 8 张地图、7 段数码管数字、道具光球、过关画面 |
| **0.5** | 2026-04-25 | 粒子系统（碎片/残骸/碰撞/出生）、炮弹互撞抵消、3 种敌坦 |
| **0.6** | 2026-04-25 | 标题画面满屏重设计、91 字字库、速射型敌坦、红坦缩体 4× |

---

## 🔮 未来计划

- [ ] 双人协作/对战模式
- [ ] 关卡编辑器（地图自定义）
- [ ] 更多道具类型（穿透弹、散弹、地雷等）
- [ ] Boss 关卡
- [ ] 音效支持（UEFI Audio 协议）
- [ ] 英文字体支持

---

## 🐛 故障排除

| 问题 | 可能原因 | 解决方案 |
|------|----------|----------|
| QEMU 黑屏 | 窗口未获得焦点 | 单击 QEMU 窗口，按空格开始 |
| 画面闪烁 | GOP 模式不支持 | 检查 GOP 是否支持 640×480 |
| 按键无响应 | QEMU 未捕获键盘 | 单击 QEMU 窗口确保焦点 |
| 中文显示空白 | 字符不在字库中 | 当前字库 91 字，检查所用字符 |
| 炮弹无法摧毁钢板 | 非穿甲弹 | 拾取「星」升级到穿甲弹 |

---

## 📄 许可证

本项目基于 **MIT License** 开源。

---

> **坦克大战（1990 经典版）** | UEFI Shell 原生游戏 | Version 0.6

[**📖 阅读英文版本**](README_EN.md)
