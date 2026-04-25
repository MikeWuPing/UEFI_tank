# 🎮 Tank Battle (1990 Classic Edition)

> Native UEFI Shell Graphics Game | Built on EDK2 + GOP Protocol | Runs on QEMU/OVMF

[![Version](https://img.shields.io/badge/version-0.6-blue)](https://github.com)
[![Platform](https://img.shields.io/badge/platform-UEFI%20Shell-brightgreen)](https://github.com)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

[**📖 中文版本**](README.md)

---


## 📖 Overview

Tank Battle (1990 Classic Edition) is a top-down fixed-map tank shooting game running natively in the **UEFI Shell** environment, faithfully recreating the core gameplay of the classic 1990 NES game *Battle City*.

The game uses UEFI **GOP (Graphics Output Protocol)** for pure software graphics rendering, runs on QEMU + OVMF virtual machines, and can also be deployed to real UEFI firmware.

<img width="624" height="469" alt="short1" src="https://github.com/user-attachments/assets/11b53fe5-207c-45f7-a23a-146b3cfddfc8" />

<img width="640" height="505" alt="Snipaste_2026-04-25_16-06-12" src="https://github.com/user-attachments/assets/7a9ee05c-627a-4f64-b878-ab43fa42eae0" />
<img width="640" height="505" alt="Snipaste_2026-04-25_16-06-36" src="https://github.com/user-attachments/assets/003bc412-f703-43e1-a599-a132e183b8bd" />


### Core Gameplay

Control a **golden tank** across a battlefield of brick walls and steel barriers. Fire shells to destroy enemy tanks that spawn from the top of the map. Protect your base (eagle icon) at the bottom center. Collect power-ups to upgrade weapons, gain extra lives, activate shields, or eliminate enemies.

| Feature | Details |
|---------|---------|
| Stages | **35 stages**, 8 maps cycling |
| Enemy Types | **4 types** (Basic, Fast, Heavy, Rapid-Fire) |
| Power-ups | **5 types** (Upgrade, Life, Shield, Freeze, Bomb) |
| Frame Rate | 30 FPS |
| Resolution | 640×480 BGRA |
| Language | Simplified Chinese (91 glyph bitmap font) |

---

## 💻 System Requirements

| Item | Requirement |
|------|-------------|
| Virtualization | QEMU 9.x+ (x86_64) |
| UEFI Firmware | OVMF (`edk2-x86_64-code.fd`) |
| Memory | Minimum 256 MB |
| Display | Supports 640×480 GOP mode (other resolutions compatible) |
| Input | Keyboard (UEFI SimpleTextIn protocol) |
| Host OS | Windows 10/11, Linux (any platform QEMU supports) |

### Development Environment (for building from source)

| Tool | Version Required |
|------|-----------------|
| Visual Studio | 2019 Professional (with MSVC 14.29) |
| EDK2 | [Tianocore Official Repository](https://github.com/tianocore/edk2) |
| Python | 3.10+ |
| QEMU | 9.x (bundled EDK2 firmware) |

---

## 🚀 Quick Start

### Run with Pre-built EFI

```bash
# 1. Prepare files
mkdir -p qemu/disk
cp Tank.efi qemu/disk/

# Create auto-startup script
cat > qemu/disk/startup.nsh << 'EOF'
echo "=== Starting Tank Battle ==="
Tank.efi
EOF

# 2. Copy QEMU's built-in EDK2 firmware
cp "C:/Program Files/qemu/share/edk2-x86_64-code.fd" qemu/OVMF_CODE.fd
cp "C:/Program Files/qemu/share/edk2-i386-vars.fd" qemu/OVMF_VARS.fd

# 3. Launch the game
qemu-system-x86_64 \
  -drive if=pflash,format=raw,file=qemu/OVMF_CODE.fd,readonly=on \
  -drive if=pflash,format=raw,file=qemu/OVMF_VARS.fd \
  -drive format=raw,file=fat:rw:qemu/disk \
  -net none -m 256
```

The UEFI Shell will auto-execute `startup.nsh` to launch the game.

### Build from Source

```bash
# 1. Copy source into EDK2
cp -r Application/Tank /path/to/edk2/EmulatorPkg/Application/Tank

# 2. Add to EmulatorPkg/EmulatorPkg.dsc under [Components]:
echo "  EmulatorPkg/Application/Tank/Tank.inf" >> EmulatorPkg/EmulatorPkg.dsc

# 3. Add to EmulatorPkg/EmulatorPkg.fdf under [FV.FvRecovery]:
echo "  INF EmulatorPkg/Application/Tank/Tank.inf" >> EmulatorPkg/EmulatorPkg.fdf

# 4. Build (Windows + VS2019)
cd /path/to/edk2
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
call edksetup.bat
build -p EmulatorPkg\EmulatorPkg.dsc -a X64 -t VS2019 -b DEBUG -m EmulatorPkg/Application/Tank/Tank.inf
```

The output `Tank.efi` will be at `Build/EmulatorX64/DEBUG_VS2019/X64/Tank.efi`.

---

## 🎮 Controls

| Key | Action |
|-----|--------|
| **Arrow Keys ↑ ↓ ← →** | Move tank |
| **Space** | Fire shell / Confirm on menu |
| **Enter** | Confirm on menu |
| **ESC** | Exit game |

### Movement Rules

- 🧱 **Brick Walls**: Destructible by shells, produces debris particles
- 🛡️ **Steel Walls**: Indestructible by normal shells; requires armor-piercing rounds
- 🌊 **Water**: Tanks cannot enter; shells pass over
- 🌲 **Forest**: Conceals tanks; shells pass through
- 🦅 **Base (Eagle)**: Impassable; game over if destroyed

---

## 🎯 Game Systems

### Map System (8 Maps)

The game uses a 26×26 tile grid (416×416 pixels). Maps cycle per stage:

| # | Name | Key Features |
|---|------|-------------|
| 1 | Classic | Symmetric bricks + steel core + water obstacles |
| 2 | Fortress | Corner steel towers + ring bricks + forest cover |
| 3 | Maze | Corridor bricks + steel gates + multi-water zones |
| 4 | Arena | Central plaza + corner steel + scattered trees |
| 5 | Gauntlet | Steel corridors + choke points + forest nodes |
| 6 | Fortress II | Four steel castles + central water + double brick arrays |
| 7 | River Crossing | River crossing + bridge battles + dual brick forts |
| 8 | Crossfire | Central cross channel + dual water + diagonal bricks |

### Stages & Waves

- **35 stages** total, 8 maps cycling
- Enemies per stage = stage number × 2 + 4 (max 20)
- Enemies spawn from 3 points at the top (left, center, right)
- Max 4 enemy tanks on field simultaneously
- Defeat all enemies to clear the stage (2-second auto-advance)

### Enemy Tank Types

| Type | Name | Color | Size | Speed | Shell | Notes |
|:---:|------|-------|------|-------|-------|-------|
| 0 | **Basic** | Silver | Standard | 15 px/s | Normal | Balanced |
| 1 | **Fast** | Red | Compact (70%) | **60 px/s** | Normal | Long barrel, white speed stripes, 4× speed |
| 2 | **Heavy** | Dark Blue | Standard | 15 px/s | Normal | Armored appearance |
| 3 | **Rapid-Fire** | Green | Standard | 15 px/s | **Fast shells** | Turret indicator, 1.6× shell speed |

### Power-up System

Defeating enemies has a **50% chance** to drop power-ups (last ~10 seconds, blink for last 2 seconds):

| Item | Icon | Effect |
|:---:|------|--------|
| ⭐ **Star** | Gold orb | **Weapon Upgrade**: Faster shells → Armor-piercing (destroys steel) |
| 💚 **Life** | Green orb | **Extra Life**: Lives +1 (max 9) |
| 💙 **Shield** | Blue orb | **Invincibility**: Blue pulsing aura, ~5 seconds |
| 🤍 **Freeze** | White orb | **Time Stop**: Enemy tanks freeze in place |
| ❤️ **Bomb** | Red orb | **Clear Screen**: Instantly destroy all enemies |

### Lives & Game Over

- Start with **3 lives**; earn +1 per stage cleared (max 9)
- 2-second invincibility on respawn after death
- Base destroyed or lives depleted → Game Over
- Clear all 35 stages → Victory

---

## 🖥️ User Interface

### Title Screen

- **Left side**: Full-area battlefield preview (dark grid background), centered «Tank Battle» golden 2× title, three demo tanks, green blinking "Press Space to Start"
- **Right panel**: Version number (0.6), controls guide, power-up guide (color blocks + descriptions), enemy type showcase

### In-Game HUD (Right Panel)

| Area | Content |
|------|---------|
| Stage | 7-segment display (supports 1-35) |
| Lives | Golden mini-tank icons × count |
| Enemies | Silver mini-tank icons × remaining |
| Score | Golden progress bar |
| Status | Upgrade / Shield indicators |
| Version | «Tank Battle 0.6» at bottom |

---

## ✨ Visual Effects

| Effect | Description |
|--------|-------------|
| 🔫 **Shell** | Round white projectile + yellow core + orange motion trail |
| 💥 **Explosion** | Orange outer → yellow middle → white core, shrinking per frame |
| 🧱 **Debris** | Brown particles with random direction + gravity + fade |
| 🛡️ **Shield Aura** | 8-direction pulsing diamond glow, blue→orange warning |
| 🌐 **Power-up Orb** | Colored glow ring + black gap + white highlight |
| ✨ **Spawn Particles** | Color particle burst on tank appearance |
| 💫 **Wreckage** | Color-matched fragment spray on destruction |

---

## 🔧 Technical Architecture

### Graphics Pipeline

| Aspect | Implementation |
|--------|---------------|
| Protocol | UEFI GOP (Graphics Output Protocol) |
| Mode | 640×480 BGRA, `PixelBlueGreenRedReserved8BitPerColor` |
| Buffer | Double-buffered in memory (1.2 MB back buffer), single `Blt` per frame |
| Frame Rate | 30 FPS (`gBS->Stall(33333)`) |
| Font | 91-glyph Chinese bitmap font (16×16 SimSun) + 7-segment display |

### Source File Structure

```
EmulatorPkg/Application/Tank/
├── Tank.inf          # EDK2 module definition
├── config.h          # Game constants & data structures
├── sprite.h          # Tile/power-up BGRA bitmap data
├── font.h            # Chinese font glyphs (91 chars, 16×16)
└── main.c            # Main program (~1500 lines)
    ├── GOP initialization & mode setting
    ├── Input polling (non-blocking ReadKeyStroke)
    ├── Game state machine (5 states)
    ├── Map system (8 × 26×26 tiles + collision detection)
    ├── Entity management (tanks, bullets, items, explosions, debris)
    ├── Enemy AI (random movement + firing + type differentiation)
    ├── Particle system (spawn, motion, gravity, fade)
    ├── Chinese font renderer (16px glyphs + 7-segment digits)
    └── Renderer (tiles, sprites, HUD, overlays)
```

### UEFI Protocol & Library Dependencies

| Protocol/Library | Purpose |
|------------------|---------|
| `Protocol/GraphicsOutput.h` | Framebuffer output, mode setting, Blt transfer |
| `Protocol/SimpleTextIn.h` | Keyboard input polling |
| `Library/UefiLib.h` | Print debug output |
| `Library/BaseMemoryLib.h` | SetMem/CopyMem |
| `Library/MemoryAllocationLib.h` | AllocatePool for back buffer |
| `Library/UefiBootServicesTableLib.h` | gBS (LocateProtocol, Stall) |
| `Library/UefiRuntimeServicesTableLib.h` | gST (ConIn input) |

### Key Technical Constraints

- **No floating-point**: All calculations use integers; trig functions simplified to rectangles
- **No standard C library**: No `<stdio.h>`, `<stdlib.h>`, `<math.h>`; pure UEFI libraries
- **Static allocation preferred**: Fixed-size global arrays for entities (MAX_ENEMIES, MAX_BULLETS, etc.)
- **UTF-8 with BOM**: Required source encoding to avoid MSVC C4819 warning

---

## 📋 Version History

| Version | Date | Changes |
|:---:|------|---------|
| **0.1** | 2026-04-25 | Initial prototype: GOP rendering, rectangle tanks, single map |
| **0.2** | 2026-04-25 | Unified pixel coordinates, BGRA color fix, 56-glyph Chinese font |
| **0.3** | 2026-04-25 | Detailed tank sprites, elongated shells, power-up system |
| **0.4** | 2026-04-25 | 8 maps, 7-segment digits, power-up orbs, stage clear screens |
| **0.5** | 2026-04-25 | Particle system, shell-vs-shell collision, 3 enemy types |
| **0.6** | 2026-04-25 | Full-screen title redesign, 91-glyph font, Rapid-Fire enemy, compact Fast tank at 4× speed |

---

## 🔮 Roadmap

- [ ] 2-player co-op / versus mode
- [ ] Stage editor (custom map designer)
- [ ] More power-up types (piercing rounds, spread shot, mines, etc.)
- [ ] Boss stages
- [ ] Audio support (UEFI Audio Protocol)
- [ ] English font support

---

## 🐛 Troubleshooting

| Problem | Possible Cause | Solution |
|---------|---------------|----------|
| QEMU black screen | Window not focused | Click QEMU window, press Space |
| Screen flickering | GOP mode issue | Verify 640×480 GOP mode supported |
| Keys not responding | QEMU not capturing keyboard | Click QEMU window to focus |
| Chinese characters blank | Character not in font | Font contains 91 characters |
| Can't destroy steel walls | Not armor-piercing | Pick up «Star» power-up to upgrade |

---

## 📄 License

This project is open-sourced under the **MIT License**.

---

> **Tank Battle (1990 Classic Edition)** | Native UEFI Shell Game | Version 0.6

[**📖 Read Chinese Version**](README.md)
