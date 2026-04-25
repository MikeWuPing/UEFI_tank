#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/GraphicsOutput.h>
#include "config.h"
#include "sprite.h"
#include "font.h"

// === Globals ===
static EFI_GRAPHICS_OUTPUT_PROTOCOL *mGop;
static UINT32 *mBackBuf;       // back buffer: SCREEN_W * SCREEN_H BGRA pixels
static UINT32 mFrameW, mFrameH;
static GameData mGame;
static BOOLEAN mKeyUp, mKeyDown, mKeyLeft, mKeyRight, mKeyFire, mKeyStart;
static UINT32 mRandSeed = 12345;

// === PRNG (simple LCG) ===
static UINT32 Rand(void) {
  mRandSeed = mRandSeed * 1103515245 + 12345;
  return (mRandSeed >> 16) & 0x7FFF;
}

// === Back buffer helpers ===
static inline void SetPixel(INT16 x, INT16 y, UINT32 color) {
  if (x >= 0 && x < (INT16)mFrameW && y >= 0 && y < (INT16)mFrameH)
    mBackBuf[(UINT32)y * mFrameW + (UINT32)x] = color;
}

static void FillRect(INT16 x, INT16 y, INT16 w, INT16 h, UINT32 color) {
  INT16 r, c;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > (INT16)mFrameW) w = (INT16)(mFrameW - (UINT32)x);
  if (y + h > (INT16)mFrameH) h = (INT16)(mFrameH - (UINT32)y);
  if (w <= 0 || h <= 0) return;
  for (r = 0; r < h; r++)
    for (c = 0; c < w; c++)
      mBackBuf[((UINT32)(y+r)) * mFrameW + (UINT32)(x+c)] = color;
}

static void DrawSprite(INT16 x, INT16 y, const UINT32 *data) {
  INT16 r, c;
  for (r = 0; r < 16; r++)
    for (c = 0; c < 16; c++) {
      UINT32 px = data[r * 16 + c];
      if (px != 0) SetPixel(x + c, y + r, px);
    }
}

static void DrawTileSprite(INT16 tx, INT16 ty, const UINT32 *data) {
  DrawSprite(MAP_X + tx * TILE_SIZE, MAP_Y + ty * TILE_SIZE, data);
}

// Format stage string "第XX关" into buf (must hold at least 8 CHAR16)
static void FormatStage(CHAR16 *buf, UINT8 stage) {
  INT16 p = 0;
  buf[p++] = L'第';
  if (stage >= 10) buf[p++] = (CHAR16)(L'0' + stage / 10);
  buf[p++] = (CHAR16)(L'0' + stage % 10);
  buf[p++] = L'关';
  buf[p] = 0;
}

// === Chinese font rendering ===
static INT16 FindCharIndex(CHAR16 ch) {
  INT16 i;
  for (i = 0; i < FONT_CHAR_COUNT; i++)
    if (fontChars[i] == ch) return i;
  return -1;
}

// Draw a single 16x16 Chinese glyph at (x, y) with given color
static void DrawGlyph(INT16 x, INT16 y, CHAR16 ch, UINT32 color) {
  INT16 idx = FindCharIndex(ch);
  INT16 r, c;
  if (idx < 0) return;
  for (r = 0; r < 16; r++) {
    UINT8 byteH = fontGlyphs[idx][r * 2];
    UINT8 byteL = fontGlyphs[idx][r * 2 + 1];
    for (c = 0; c < 8; c++) {
      if (byteH & (1 << (7 - c))) SetPixel(x + c, y + r, color);
      if (byteL & (1 << (7 - c))) SetPixel(x + 8 + c, y + r, color);
    }
  }
}

// 7-segment digit drawing (10x16 pixel cell)
static void DrawDigit(INT16 x, INT16 y, UINT8 digit, UINT32 color) {
  // Segments: a(top), b(top-right), c(bot-right), d(bot), e(bot-left), f(top-left), g(mid)
  // Segment patterns for 0-9 (1=lit)
  static const UINT8 segs[10] = {
    0x3F, // 0: a b c d e f
    0x06, // 1: b c
    0x5B, // 2: a b d e g
    0x4F, // 3: a b c d g
    0x66, // 4: b c f g
    0x6D, // 5: a c d f g
    0x7D, // 6: a c d e f g
    0x07, // 7: a b c
    0x7F, // 8: a b c d e f g
    0x6F, // 9: a b c d f g
  };
  UINT8 s = segs[digit];
  // Horizontal: a (top), g (mid), d (bot) — each 6x2
  if (s & 0x01) FillRect(x + 2, y,      6, 2, color); // a
  if (s & 0x40) FillRect(x + 2, y + 7,   6, 2, color); // g
  if (s & 0x08) FillRect(x + 2, y + 14,  6, 2, color); // d
  // Vertical: f (top-left), e (bot-left), b (top-right), c (bot-right) — each 2x6
  if (s & 0x20) FillRect(x,     y + 2,   2, 5, color); // f
  if (s & 0x10) FillRect(x,     y + 9,   2, 5, color); // e
  if (s & 0x02) FillRect(x + 8, y + 2,   2, 5, color); // b
  if (s & 0x04) FillRect(x + 8, y + 9,   2, 5, color); // c
}

// Draw Chinese text string at (x, y), optionally 2x scaled
static void DrawText(INT16 x, INT16 y, const CHAR16 *str, UINT32 color, BOOLEAN big) {
  INT16 cx = x, cy = y;
  INT16 i;
  for (i = 0; str[i] != 0; i++) {
    CHAR16 ch = str[i];
    if (ch == ' ') { cx += (big ? 16 : 10); continue; }
    // Numbers 0-9: 7-segment display
    if (ch >= L'0' && ch <= L'9') {
      if (big) {
        DrawDigit(cx, cy, (UINT8)(ch - L'0'), color);
        DrawDigit(cx + 10, cy + 16, (UINT8)(ch - L'0'), color); // double size
        // Actually just scale: draw bigger
        cx -= 10; // undo
        // Simpler: draw at double size using FillRect for segments
        { UINT8 d = (UINT8)(ch - L'0');
          static const UINT8 s2[10] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};
          UINT8 ss = s2[d];
          if (ss & 0x01) FillRect(cx + 4, cy,      12, 4, color);
          if (ss & 0x40) FillRect(cx + 4, cy + 14,  12, 4, color);
          if (ss & 0x08) FillRect(cx + 4, cy + 28,  12, 4, color);
          if (ss & 0x20) FillRect(cx,     cy + 4,    4, 10, color);
          if (ss & 0x10) FillRect(cx,     cy + 18,   4, 10, color);
          if (ss & 0x02) FillRect(cx + 16, cy + 4,    4, 10, color);
          if (ss & 0x04) FillRect(cx + 16, cy + 18,   4, 10, color);
        }
        cx += 24;
      } else {
        DrawDigit(cx, cy, (UINT8)(ch - L'0'), color);
        cx += 12;
      }
      continue;
    }
    // Chinese glyph
    if (big) {
      INT16 idx2 = FindCharIndex(ch);
      if (idx2 >= 0) {
        INT16 r, c;
        for (r = 0; r < 16; r++) {
          UINT8 bh = fontGlyphs[idx2][r * 2];
          UINT8 bl = fontGlyphs[idx2][r * 2 + 1];
          for (c = 0; c < 8; c++) {
            if (bh & (1 << (7 - c))) FillRect(cx + c*2, cy + r*2, 2, 2, color);
            if (bl & (1 << (7 - c))) FillRect(cx + 16 + c*2, cy + r*2, 2, 2, color);
          }
        }
      }
      cx += 32;
    } else {
      DrawGlyph(cx, cy, ch, color);
      cx += 16;
    }
  }
}

// === Draw tank (32x32 normal, or compact for fast tanks) ===
static void DrawTankProc(INT16 px, INT16 py, UINT8 dir, UINT32 body, UINT32 dark, UINT32 track, BOOLEAN compact) {
  INT16 r;
  INT16 o  = compact ? 5 : 0;   // inset
  INT16 tk = compact ? 5 : 7;   // track width
  INT16 hx = compact ? 11 : 18; // hull width

  if (dir == DIR_UP || dir == DIR_DOWN) {
    INT16 top = py + o;
    INT16 midH = py + 15;

    // Left track
    FillRect(px + o, top, tk, 30 - o*2, track);
    FillRect(px + o, top + 1, tk, 1, COLOR_LTGREY);
    for (r = 0; r < (compact?10:14); r++) {
      FillRect(px + o + 2, top + 2 + r*(compact?3:2), compact?2:3, 1, COLOR_LTGREY);
    }

    // Right track
    FillRect(px + 32 - tk - o, top, tk, 30 - o*2, track);
    FillRect(px + 32 - tk - o, top + 1, tk, 1, COLOR_LTGREY);
    for (r = 0; r < (compact?10:14); r++) {
      FillRect(px + 30 - o - tk + 2, top + 2 + r*(compact?3:2), compact?2:3, 1, COLOR_LTGREY);
    }

    // Hull
    FillRect(px + tk + o, top + 2, hx, 26 - o*2, body);
    FillRect(px + tk + o + 1, top + 6, 2, 20 - o*2, body);
    FillRect(px + tk + o + hx - 3, top + 6, 2, 20 - o*2, dark);

    // Turret (simplified for compact)
    if (!compact) {
      FillRect(px + 12, midH - 3, 8, 10, dark);
      FillRect(px + 11, midH - 5, 10, 10, body);
      FillRect(px + 12, midH + 3, 8, 2, COLOR_GOLD);
      FillRect(px + 14, midH - 3, 4, 3, dark);
    }
    FillRect(px + (compact?13:11), midH - (compact?3:5), (compact?6:10), (compact?6:10), body);

    // Cannon
    if (dir == DIR_UP) {
      FillRect(px + 14, midH - 12, 4, 8, dark);
      FillRect(px + 15, midH - 12, 2, 8, COLOR_GREY);
      FillRect(px + (compact?12:13), midH - (compact?12:14), (compact?8:6), 3, COLOR_LTGREY);
    } else {
      FillRect(px + 14, midH + 4, 4, 8, dark);
      FillRect(px + 15, midH + 4, 2, 8, COLOR_GREY);
      FillRect(px + (compact?12:13), midH + (compact?9:11), (compact?8:6), 3, COLOR_LTGREY);
    }
  } else {
    // LEFT or RIGHT orientation
    INT16 left = px, right = px + 29;
    INT16 midW = px + 15;

    // Top track
    FillRect(left, py, 30, 7, track);
    FillRect(left + 1, py, 28, 1, COLOR_LTGREY);
    for (r = 0; r < 14; r++) {
      FillRect(left + 2 + r*2, py + 2, 1, 3, COLOR_LTGREY);
    }
    for (r = 0; r < 4; r++) {
      FillRect(left + 4 + r*7, py + 1, 4, 5, COLOR_GREY);
      FillRect(left + 5 + r*7, py + 2, 2, 3, COLOR_LTGREY);
    }

    // Bottom track
    FillRect(left, py + 25, 30, 7, track);
    FillRect(left + 1, py + 25, 28, 1, COLOR_LTGREY);
    for (r = 0; r < 14; r++) {
      FillRect(left + 2 + r*2, py + 27, 1, 3, COLOR_LTGREY);
    }
    for (r = 0; r < 4; r++) {
      FillRect(left + 4 + r*7, py + 26, 4, 5, COLOR_GREY);
      FillRect(left + 5 + r*7, py + 27, 2, 3, COLOR_LTGREY);
    }

    // Hull
    FillRect(left + 2, py + 7, 26, 18, body);
    FillRect(left + 3, py + 8, 3, 16, body);
    FillRect(left + 24, py + 8, 3, 16, dark);
    FillRect(left + 3, py + 7, 24, 2, COLOR_GOLD);
    FillRect(left + 3, py + 23, 24, 2, dark);
    FillRect(left + 5, py + 12, 1, 8, dark);
    FillRect(left + 16, py + 12, 1, 8, dark);

    // Engine/Armor plates
    if (dir == DIR_LEFT) {
      FillRect(right - 6, py + 7, 4, 18, track);
      FillRect(right - 5, py + 8, 2, 16, COLOR_LTGREY);
    } else {
      FillRect(left + 2, py + 7, 4, 18, track);
      FillRect(left + 3, py + 8, 2, 16, COLOR_LTGREY);
    }

    // Turret
    FillRect(midW - 3, py + 12, 6, 8, dark);
    FillRect(midW - 4, py + 11, 8, 10, body);
    FillRect(midW - 2, py + 11, 4, 10, body);
    FillRect(midW - 1, py + 19, 2, 2, COLOR_GOLD);
    FillRect(midW - 1, py + 15, 2, 2, dark);
    FillRect(midW, py + 16, 1, 1, COLOR_LTGREY);

    // Cannon
    if (dir == DIR_LEFT) {
      FillRect(midW - 12, py + 14, 8, 4, dark);
      FillRect(midW - 12, py + 15, 8, 2, COLOR_GREY);
      FillRect(midW - 14, py + 14, 3, 4, COLOR_LTGREY);
      FillRect(midW - 13, py + 15, 1, 2, COLOR_WHITE);
      FillRect(midW - 4, py + 13, 3, 6, dark);
    } else {
      FillRect(midW + 4, py + 14, 8, 4, dark);
      FillRect(midW + 4, py + 15, 8, 2, COLOR_GREY);
      FillRect(midW + 11, py + 14, 3, 4, COLOR_LTGREY);
      FillRect(midW + 12, py + 15, 1, 2, COLOR_WHITE);
      FillRect(midW + 1, py + 13, 3, 6, dark);
    }
  }
}

// === Draw tank with shield overlay ===
static void DrawTankGame(Tank *t, BOOLEAN isPlayer) {
  INT16 px = MAP_X + t->x;
  INT16 py = MAP_Y + t->y;
  UINT32 body, dark, track;
  if (isPlayer) {
    body = COLOR_GOLD; dark = COLOR_DKGOLD; track = COLOR_DARKGREY;
  } else {
    switch (t->type) {
      case 0: // Basic - silver
        body = COLOR_LTGREY; dark = COLOR_GREY; track = COLOR_DARKGREY; break;
      case 1: // Fast - red
        body = COLOR_RED; dark = COLOR_DKRED; track = COLOR_DARKGREY; break;
      case 2: // Heavy - dark blue
        body = 0x000080C0; dark = 0x00004080; track = COLOR_DARKGREY; break;
      case 3: // Stealth - green
        body = COLOR_GREEN; dark = COLOR_DKGREEN; track = COLOR_DARKGREY; break;
      default: body = COLOR_LTGREY; dark = COLOR_GREY; track = COLOR_DARKGREY; break;
    }
  }
  DrawTankProc(px, py, t->dir, body, dark, track, (!isPlayer && t->type == 1));

  // Fast tank (type 1): longer cannon barrel
  if (!isPlayer && t->type == 1) {
    switch (t->dir) {
      case DIR_UP:    FillRect(px + 14, py - 12, 4, 6, dark); FillRect(px + 15, py - 12, 2, 6, COLOR_LTGREY); FillRect(px + 14, py - 14, 4, 2, COLOR_WHITE); break;
      case DIR_DOWN:  FillRect(px + 14, py + 36, 4, 6, dark); FillRect(px + 15, py + 36, 2, 6, COLOR_LTGREY); FillRect(px + 14, py + 40, 4, 2, COLOR_WHITE); break;
      case DIR_LEFT:  FillRect(px - 12, py + 14, 6, 4, dark); FillRect(px - 12, py + 15, 6, 2, COLOR_LTGREY); FillRect(px - 14, py + 14, 2, 4, COLOR_WHITE); break;
      case DIR_RIGHT: FillRect(px + 38, py + 14, 6, 4, dark); FillRect(px + 38, py + 15, 6, 2, COLOR_LTGREY); FillRect(px + 42, py + 14, 2, 4, COLOR_WHITE); break;
    }
    FillRect(px + 12, py + 6, 1, 20, COLOR_WHITE);
    FillRect(px + 19, py + 6, 1, 20, COLOR_WHITE);
  }
  // Rapid-fire (type 3): muzzle flash dot
  if (!isPlayer && t->type == 3) {
    FillRect(px + TANK_PX_W/2 - 2, py + TANK_PX_H/2 - 2, 4, 4, COLOR_YELLOW);
  }

  // Invincibility: pulsing diamond corner glow
  if (t->invincible > 0) {
    UINT32 color = (t->invincible < 60) ? COLOR_ORANGE : COLOR_BLUE;
    INT16 phase = t->invincible & 7;  // 0-7 animation phase
    INT16 dist = 3 + (phase & 3);     // 3-6 pixel corner offset
    INT16 cx = px + TANK_PX_W/2;
    INT16 cy = py + TANK_PX_H/2;
    if (phase < 4) {
      // Corner diamonds
      FillRect(cx - dist - 2, py, 3, 3, color);
      FillRect(cx + dist - 1, py, 3, 3, color);
      FillRect(cx - dist - 2, py + TANK_PX_H - 3, 3, 3, color);
      FillRect(cx + dist - 1, py + TANK_PX_H - 3, 3, 3, color);
      FillRect(px, cy - dist - 2, 3, 3, color);
      FillRect(px + TANK_PX_W - 3, cy - dist - 2, 3, 3, color);
      FillRect(px, cy + dist - 1, 3, 3, color);
      FillRect(px + TANK_PX_W - 3, cy + dist - 1, 3, 3, color);
    }
    // Center glow
    SetPixel(cx, cy, COLOR_WHITE);
  }
}

// === Draw bullet (round shell with motion trail) ===
static void DrawBulletGame(Bullet *b) {
  INT16 cx = MAP_X + b->x;
  INT16 cy = MAP_Y + b->y;

  // Motion trail (behind the bullet)
  {
    INT16 tx = cx, ty = cy;
    switch (b->dir) {
      case DIR_UP:    ty += 6; break;
      case DIR_DOWN:  ty -= 6; break;
      case DIR_LEFT:  tx += 6; break;
      case DIR_RIGHT: tx -= 6; break;
    }
    FillRect(tx - 2, ty - 2, 4, 4, COLOR_ORANGE);
    FillRect(tx - 1, ty - 1, 2, 2, COLOR_YELLOW);
  }

  // Main shell body (round)
  FillRect(cx - 2, cy - 3, 5, 6, COLOR_WHITE);
  FillRect(cx - 3, cy - 2, 7, 4, COLOR_WHITE);
  FillRect(cx - 1, cy - 2, 3, 4, COLOR_YELLOW);
  FillRect(cx, cy - 1, 1, 2, COLOR_ORANGE);

  // Highlight dot
  SetPixel(cx, cy, COLOR_WHITE);
}

// === Draw explosion ===
static void DrawExplosionGame(Explosion *e) {
  INT16 ex = MAP_X + e->x, ey = MAP_Y + e->y;
  INT16 r = 6 - e->timer / 8;
  if (r < 1) r = 1;
  if (r > 10) r = 10;
  FillRect(ex - r, ey - r, r * 2, r * 2, COLOR_ORANGE);
  FillRect(ex - r/2, ey - r/2, r, r, COLOR_YELLOW);
  FillRect(ex - r/4, ey - r/4, r/2, r/2, COLOR_WHITE);
}

// === Draw power-up as glowing orb with Chinese label ===
static void DrawPowerUpGame(PowerUp *p) {
  INT16 cx = MAP_X + p->x;
  INT16 cy = MAP_Y + p->y;
  UINT32 color, glow;
  const CHAR16 *label;
  switch (p->type) {
    case 0: color = COLOR_GOLD;   glow = COLOR_YELLOW;  label = L"星"; break;
    case 1: color = COLOR_GREEN;  glow = COLOR_GREEN;   label = L"命"; break;
    case 2: color = COLOR_BLUE;   glow = COLOR_BLUE;    label = L"护"; break;
    case 3: color = COLOR_LTGREY; glow = COLOR_WHITE;   label = L"停"; break;
    case 4: color = COLOR_RED;    glow = COLOR_RED;     label = L"炸"; break;
    default:color = COLOR_GOLD;   glow = COLOR_YELLOW;  label = L"?";  break;
  }
  // Blinking before disappearing
  if (p->timer < 60 && (p->timer & 4)) return;

  // Outer glow ring
  FillRect(cx - 7, cy - 4, 14, 8, glow);
  FillRect(cx - 4, cy - 7, 8, 14, glow);
  FillRect(cx - 5, cy - 5, 10, 10, COLOR_BLACK);
  // Inner colored orb
  FillRect(cx - 4, cy - 3, 8, 6, color);
  FillRect(cx - 3, cy - 4, 6, 8, color);
  FillRect(cx - 2, cy - 2, 4, 4, COLOR_WHITE);
  // Label below
  DrawText(cx - 8, cy + 9, label, glow, FALSE);
}

// === Map data (5 stage layouts) ===
// 0=empty 1=brick 2=steel 3=water 4=tree 5=base
#define MAP_COUNT 8
static const UINT8 mMaps[MAP_COUNT][MAP_ROWS][MAP_COLS] = {
  // === Map 0: Classic layout ===
  {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0},
    {0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0},
    {0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0},
    {0,0,1,1,0,0,0,0,0,0,2,2,2,2,0,0,0,0,0,0,0,0,1,1,0,0},
    {0,0,1,1,0,0,0,0,0,0,2,2,2,2,0,0,0,0,0,0,0,0,1,1,0,0},
    {0,0,0,0,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,0,0,0,0},
    {0,0,3,3,0,0,1,1,0,0,4,4,0,0,4,4,0,0,1,1,0,0,3,3,0,0},
    {0,0,3,3,0,0,1,1,0,0,4,4,0,0,4,4,0,0,1,1,0,0,3,3,0,0},
    {0,0,0,0,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,0,0,0,0},
    {0,0,1,1,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,1,1,0,0},
    {0,0,1,1,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,1,1,0,0},
    {0,0,1,1,0,0,3,3,0,0,2,2,2,2,0,0,3,3,0,0,0,0,1,1,0,0},
    {0,0,1,1,0,0,3,3,0,0,2,2,2,2,0,0,3,3,0,0,0,0,1,1,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
  },
  // === Map 1: Fortress ===
  {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,2,2,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,2,2,0,0},
    {0,0,2,2,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,2,2,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,1,1,0,0,1,1,0,0,0,0,0,0,1,1,0,0,1,1,0,0,1,1},
    {1,1,0,0,1,1,0,0,1,1,0,0,0,0,0,0,1,1,0,0,1,1,0,0,1,1},
    {0,0,0,0,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,0,0,0,0},
    {1,1,0,0,1,1,0,0,3,3,0,0,1,1,0,0,3,3,0,0,1,1,0,0,1,1},
    {1,1,0,0,1,1,0,0,3,3,0,0,1,1,0,0,3,3,0,0,1,1,0,0,1,1},
    {0,0,0,0,0,0,1,1,0,0,0,0,2,2,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,2,2,0,0,0,0,1,1,0,0,0,0,0,0},
    {1,1,0,0,1,1,0,0,4,4,0,0,0,0,0,0,4,4,0,0,1,1,0,0,1,1},
    {1,1,0,0,1,1,0,0,4,4,0,0,0,0,0,0,4,4,0,0,1,1,0,0,1,1},
    {0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0},
    {0,0,2,2,0,0,0,0,0,0,3,3,0,0,3,3,0,0,0,0,0,0,2,2,0,0},
    {0,0,2,2,0,0,0,0,0,0,3,3,0,0,3,3,0,0,0,0,0,0,2,2,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
  },
  // === Map 2: Maze ===
  {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,1,1,0,0,0,1,1,1,0,0,0,0,1,1,1,0,0,0,1,1,1,0,0},
    {0,0,2,0,0,0,1,0,0,0,0,0,1,1,0,0,0,0,0,1,0,0,0,2,0,0},
    {0,0,2,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,2,0,0,0},
    {0,0,0,0,1,0,0,0,1,0,0,0,1,1,0,0,0,1,0,0,0,1,0,0,0,0},
    {0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0},
    {0,0,1,1,1,0,0,0,1,1,1,0,0,0,0,1,1,1,0,0,0,1,1,1,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,1,0,0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0,0},
    {0,0,0,0,0,1,0,0,0,0,1,1,0,0,1,1,0,0,0,0,1,0,0,0,0,0},
    {0,0,3,3,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,3,3,0,0},
    {0,0,3,3,0,0,0,1,1,0,0,0,1,1,0,0,0,1,1,0,0,0,3,3,0,0},
    {0,0,0,0,0,1,0,0,0,0,1,1,0,0,1,1,0,0,0,0,1,0,0,0,0,0},
    {0,0,1,1,0,0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,4,4,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,4,4,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,1,0,0,1,0,0,0,1,1,0,0,1,1,0,0,0,1,0,0,1,1,0,0},
    {0,0,0,1,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,1,0,0,0},
    {0,0,0,0,0,0,1,0,0,0,0,0,1,1,0,0,0,0,0,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,0,0,0,0,0,1,1,0,0,0,0,0,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
  },
  // === Map 3: Arena ===
  {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,0,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,0,1,0,0},
    {0,0,1,0,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,0,1,0,0},
    {0,0,1,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,1,0,0},
    {0,0,0,0,1,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0},
    {0,0,0,0,0,0,1,0,0,0,2,0,0,0,0,2,0,0,0,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,2,0,0,0,3,3,0,0,0,2,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,2,0,0,0,3,3,0,0,0,2,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,0,0,0,4,0,0,0,0,4,0,0,0,1,0,0,0,0,0,0},
    {0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1,0,0,0,0},
    {0,0,1,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,1,0,0},
    {0,0,1,0,0,0,0,0,2,0,0,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0},
    {0,0,1,0,0,0,0,0,2,0,0,0,0,0,0,0,0,2,0,0,0,0,0,1,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,4,4,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,4,4,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,1,1,0,0,0,1,1,0,0,0,0,1,1,0,0,0,1,1,0,0,0,0},
    {0,0,0,0,1,1,0,0,0,1,1,0,0,0,0,1,1,0,0,0,1,1,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
  },
  // === Map 4: Crossfire ===
  {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,0,0,0,1,0,0,0,2,0,0,0,0,2,0,0,0,1,0,0,0,1,0,0},
    {0,0,1,0,0,0,1,0,0,0,2,0,0,0,0,2,0,0,0,1,0,0,0,1,0,0},
    {0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,1,1,0,0,0,0,0,0,4,4,0,0,0,0,0,0,1,1,0,0,0,0},
    {0,0,0,0,1,1,0,0,0,0,0,0,4,4,0,0,0,0,0,0,1,1,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,3,3,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,3,3,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0},
    {0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0},
    {0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
  },
  // === Map 5: Gauntlet ===
  {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,2,2,2,2,2,2,0,0,1,1,0,0,1,1,0,0,2,2,2,2,2,2,0,0},
    {0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0},
    {0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0},
    {0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0},
    {0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0},
    {0,0,1,1,0,0,0,0,0,0,0,0,3,3,0,0,0,0,0,0,0,0,1,1,0,0},
    {0,0,1,1,0,0,0,0,0,0,0,0,3,3,0,0,0,0,0,0,0,0,1,1,0,0},
    {0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,2,2,2,2,2,2,0,0,1,1,0,0,1,1,0,0,2,2,2,2,2,2,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,4,4,0,0,4,4,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,4,4,0,0,4,4,0,0,0,0,0,0,0,0,0,0},
    {0,0,2,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,2,0,0},
    {0,0,2,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,2,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
  },
  // === Map 6: Fortress II ===
  {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,2,2,2,2,0,0,2,2,0,0,0,0,0,0,2,2,0,0,2,2,2,2,0,0},
    {0,0,2,0,0,2,0,0,2,2,0,0,0,0,0,0,2,2,0,0,2,0,0,2,0,0},
    {0,0,2,0,0,2,0,0,0,0,1,1,0,0,1,1,0,0,0,0,2,0,0,2,0,0},
    {0,0,2,0,0,2,0,0,0,0,1,1,0,0,1,1,0,0,0,0,2,0,0,2,0,0},
    {0,0,2,2,2,2,0,0,0,0,0,0,1,1,0,0,0,0,0,0,2,2,2,2,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0},
    {0,0,1,1,0,0,0,0,0,0,0,0,3,3,0,0,0,0,0,0,0,0,1,1,0,0},
    {0,0,1,1,0,0,0,0,0,0,0,0,3,3,0,0,0,0,0,0,0,0,1,1,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,4,4,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,4,4,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0},
    {0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,2,2,2,2,0,0,0,0,0,0,0,0,0,0,2,2,2,2,0,0,0,0},
    {0,0,0,0,2,0,0,2,0,0,0,0,0,0,0,0,0,0,2,0,0,2,0,0,0,0},
    {0,0,0,0,2,0,0,2,0,0,1,1,0,0,1,1,0,0,2,0,0,2,0,0,0,0},
    {0,0,0,0,2,0,0,2,0,0,1,1,0,0,1,1,0,0,2,0,0,2,0,0,0,0},
    {0,0,0,0,2,2,2,2,0,0,0,0,0,0,0,0,0,0,2,2,2,2,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
  },
  // === Map 7: River Crossing ===
  {
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0},
    {0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0},
    {0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,2,3,3,3,3,2,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,2,3,2,2,3,2,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,2,3,2,2,3,2,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,1,0,0,0,0,0,0,2,3,3,3,3,2,0,0,0,0,0,0,1,1,0,0},
    {0,0,1,1,0,0,0,0,0,0,2,2,2,2,2,2,0,0,0,0,0,0,1,1,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,4,4,0,0,4,4,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,4,4,0,0,4,4,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,1,0,0,0,0,0,0,0,0,2,2,0,0,0,0,0,0,0,0,1,1,0,0},
    {0,0,1,1,0,0,0,0,0,0,0,0,2,2,0,0,0,0,0,0,0,0,1,1,0,0},
    {0,0,0,0,0,0,2,2,2,2,0,0,0,0,0,0,2,2,2,2,0,0,0,0,0,0},
    {0,0,0,0,0,0,2,2,2,2,0,0,0,0,0,0,2,2,2,2,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0},
    {0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,1,1,5,5,1,1,1,1,1,1,0,0,0,0,0,0},
  },
};

// === Get tile at pixel position ===
static UINT8 GetTile(INT16 x, INT16 y) {
  INT16 tx = x / TILE_SIZE;
  INT16 ty = y / TILE_SIZE;
  if (tx < 0 || tx >= MAP_COLS || ty < 0 || ty >= MAP_ROWS) return TILE_STEEL;
  return mGame.map.tiles[ty][tx];
}

// === Collision: check if tank area overlaps blocking tiles ===
static BOOLEAN TankCollides(INT16 tx, INT16 ty, UINT8 checkWater) {
  INT16 tlx = tx, tly = ty;
  INT16 brx = tx + TANK_PX_W - 1, bry = ty + TANK_PX_H - 1;
  INT16 r, c;
  for (r = tly; r <= bry; r += TILE_SIZE) {
    for (c = tlx; c <= brx; c += TILE_SIZE) {
      UINT8 t = GetTile(c, r);
      if (t == TILE_BRICK || t == TILE_STEEL || t == TILE_BASE) return TRUE;
      if (checkWater && t == TILE_WATER) return TRUE;
    }
  }
  // Map boundary
  if (tlx < 0 || tly < 0 || brx >= MAP_PX_W || bry >= MAP_PX_H) return TRUE;
  return FALSE;
}

// === Tank vs tank collision (pixel-based) ===
static BOOLEAN TanksOverlap(Tank *a, Tank *b) {
  INT16 ax, ay, bx, by;
  if (!a->alive || !b->alive) return FALSE;
  ax = a->x; ay = a->y;
  bx = b->x; by = b->y;
  return (
    ax < bx + TANK_PX_W - 2 && ax + TANK_PX_W - 2 > bx &&
    ay < by + TANK_PX_H - 2 && ay + TANK_PX_H - 2 > by
  );
}

// === Bullet vs tile collision ===
static UINT8 BulletHitsTile(Bullet *b) {
  INT16 cx = b->x;
  INT16 cy = b->y;
  INT16 tlx = cx - 2, tly = cy - 2;
  INT16 brx = cx + 2, bry = cy + 2;
  INT16 r, c;
  for (r = tly; r <= bry; r += TILE_SIZE) {
    for (c = tlx; c <= brx; c += TILE_SIZE) {
      UINT8 t = GetTile(c, r);
      if (b->piercing) {
        if (t == TILE_BRICK) return TILE_BRICK;
        if (t == TILE_STEEL) return TILE_STEEL;
      } else {
        if (t == TILE_BRICK || t == TILE_STEEL) return t;
      }
      if (t == TILE_BASE) return TILE_BASE;
    }
  }
  return TILE_EMPTY;
}

// === Spawn debris particles ===
static void SpawnDebris(INT16 x, INT16 y, UINT32 color) {
  INT16 i, n;
  for (i = 0, n = 0; i < MAX_DEBRIS && n < 6; i++) {
    if (!mGame.debris[i].alive) {
      mGame.debris[i].x = x;
      mGame.debris[i].y = y;
      mGame.debris[i].vx = (INT16)((Rand() % 5) - 2);
      mGame.debris[i].vy = (INT16)((Rand() % 5) - 5);
      mGame.debris[i].timer = 12 + (UINT8)(Rand() % 12);
      mGame.debris[i].alive = TRUE;
      mGame.debris[i].color = color;
      n++;
    }
  }
}

// === Update debris ===
static void UpdateDebris(void) {
  INT16 i;
  for (i = 0; i < MAX_DEBRIS; i++) {
    if (mGame.debris[i].alive) {
      mGame.debris[i].x += mGame.debris[i].vx;
      mGame.debris[i].y += mGame.debris[i].vy;
      mGame.debris[i].vy += 1; // gravity
      if (mGame.debris[i].timer > 0)
        mGame.debris[i].timer--;
      else
        mGame.debris[i].alive = FALSE;
    }
  }
}

// === Draw debris ===
static void DrawDebrisGame(Debris *d) {
  INT16 sx = MAP_X + d->x;
  INT16 sy = MAP_Y + d->y;
  INT16 sz = (d->timer > 8) ? 2 : 1;
  FillRect(sx, sy, sz, sz, d->color);
}

// === Destroy bricks in bullet radius ===
static void DestroyBricks(INT16 px, INT16 py) {
  INT16 r, c;
  for (r = py - TILE_SIZE; r <= py + TILE_SIZE; r += TILE_SIZE) {
    for (c = px - TILE_SIZE; c <= px + TILE_SIZE; c += TILE_SIZE) {
      INT16 tx = c / TILE_SIZE;
      INT16 ty = r / TILE_SIZE;
      if (tx >= 0 && tx < MAP_COLS && ty >= 0 && ty < MAP_ROWS) {
        if (mGame.map.tiles[ty][tx] == TILE_BRICK) {
          mGame.map.tiles[ty][tx] = TILE_EMPTY;
          SpawnDebris(c + TILE_SIZE/2, r + TILE_SIZE/2, COLOR_BROWN);
        }
      }
    }
  }
}

// === Spawn explosion ===
static void SpawnExplosion(INT16 x, INT16 y) {
  INT16 i;
  for (i = 0; i < MAX_EXPLOSIONS; i++) {
    if (!mGame.explosions[i].alive) {
      mGame.explosions[i].x = x;
      mGame.explosions[i].y = y;
      mGame.explosions[i].timer = 48;
      mGame.explosions[i].alive = TRUE;
      break;
    }
  }
}

// === Spawn power-up ===
static void SpawnPowerUp(INT16 x, INT16 y) {
  INT16 i, r;
  for (i = 0; i < MAX_POWERUPS; i++) {
    if (!mGame.powerUps[i].alive) {
      mGame.powerUps[i].x = (x / TILE_SIZE) * TILE_SIZE;
      mGame.powerUps[i].y = (y / TILE_SIZE) * TILE_SIZE;
      r = Rand() % 5;
      mGame.powerUps[i].type = (UINT8)r;
      mGame.powerUps[i].alive = TRUE;
      mGame.powerUps[i].timer = POWERUP_LIFE;
      break;
    }
  }
}

// === Fire bullet ===
static void FireBullet(Tank *t, BOOLEAN fromPlayer) {
  INT16 i, bx, by;
  for (i = 0; i < MAX_BULLETS; i++) {
    if (!mGame.bullets[i].alive) {
      bx = t->x + TANK_PX_W / 2;
      by = t->y + TANK_PX_H / 2;
      mGame.bullets[i].dir = t->dir;
      mGame.bullets[i].speed = (t->upgrade >= 1) ? FAST_BULLET : BULLET_SPEED;
      mGame.bullets[i].fromPlayer = fromPlayer;
      mGame.bullets[i].piercing = (t->upgrade >= 2);
      switch (t->dir) {
        case DIR_UP:    bx -= BULLET_W/2; by = t->y; break;
        case DIR_DOWN:  bx -= BULLET_W/2; by = t->y + TANK_PX_H; break;
        case DIR_LEFT:  bx = t->x; by -= BULLET_H/2; break;
        case DIR_RIGHT: bx = t->x + TANK_PX_W; by -= BULLET_H/2; break;
      }
      mGame.bullets[i].x = bx;
      mGame.bullets[i].y = by;
      mGame.bullets[i].alive = TRUE;
      t->shootCd = fromPlayer ? PLAYER_SHOOT_CD : ENEMY_SHOOT_CD * 2;
      break;
    }
  }
}

// === Initialize game ===
static void InitGame(INT16 stage) {
  INT16 r, c, i;

  // Load map based on stage (cycle through 5 maps)
  {
    INT16 mapIdx = (stage - 1) % MAP_COUNT;
    for (r = 0; r < MAP_ROWS; r++)
      for (c = 0; c < MAP_COLS; c++)
        mGame.map.tiles[r][c] = mMaps[mapIdx][r][c];
  }

  mGame.map.stage = (UINT8)stage;
  mGame.map.enemyTotal = (UINT8)(stage * 2 + 4);
  if (mGame.map.enemyTotal > 20) mGame.map.enemyTotal = 20;
  mGame.map.enemiesLeft = mGame.map.enemyTotal;
  mGame.map.enemyCount = 0;
  mGame.state = GS_PLAYING;
  mGame.spawnTimer = SPAWN_DELAY;
  mGame.enemiesSpawned = 0;
  mGame.enemiesAlive = 0;
  mGame.stageIntro = 60;
  mGame.pause = 0;

  // Init player (pixel coordinates)
  mGame.player.x = 12 * TILE_SIZE;
  mGame.player.y = 22 * TILE_SIZE;
  mGame.player.dir = DIR_UP;
  mGame.player.speed = PLAYER_SPEED;
  mGame.player.alive = TRUE;
  mGame.player.type = 0;
  mGame.player.shootCd = 0;
  mGame.player.invincible = 120;
  SpawnDebris(mGame.player.x + TANK_PX_W/2, mGame.player.y + TANK_PX_H/2, COLOR_GOLD);
  SpawnDebris(mGame.player.x + TANK_PX_W/2, mGame.player.y + TANK_PX_H/2, COLOR_GOLD);
  mGame.player.upgrade = 0;
  mGame.player.color = 0;

  // Clear enemies
  for (i = 0; i < MAX_ENEMIES; i++)
    mGame.enemies[i].alive = FALSE;

  // Clear bullets
  for (i = 0; i < MAX_BULLETS; i++)
    mGame.bullets[i].alive = FALSE;

  // Clear power-ups
  for (i = 0; i < MAX_POWERUPS; i++)
    mGame.powerUps[i].alive = FALSE;

  // Clear explosions
  for (i = 0; i < MAX_EXPLOSIONS; i++)
    mGame.explosions[i].alive = FALSE;
  // Clear debris
  for (i = 0; i < MAX_DEBRIS; i++)
    mGame.debris[i].alive = FALSE;
}

// === Reset entire game ===
static void ResetGame(void) {
  mGame.state = GS_TITLE;
  mGame.lives = 3;
  mGame.score = 0;
  mGame.frame = 0;
  mGame.titleBlink = 0;
  InitGame(1);
  mGame.state = GS_TITLE;
}

// === Spawn enemy ===
static void SpawnEnemy(void) {
  INT16 i, spawnX, spawnY;
  UINT8 spawnPoints[3][2] = { {0,0}, {12,0}, {24,0} };
  UINT8 pt;

  if (mGame.enemiesSpawned >= mGame.map.enemyTotal) return;
  if (mGame.enemiesAlive >= MAX_ENEMIES) return;

  for (i = 0; i < MAX_ENEMIES; i++) {
    if (!mGame.enemies[i].alive) {
      pt = (UINT8)(Rand() % 3);
      spawnX = spawnPoints[pt][0] * TILE_SIZE;
      spawnY = 0;

      // Don't spawn on top of another tank
      {
        INT16 j;
        BOOLEAN blocked = FALSE;
        for (j = 0; j < MAX_ENEMIES; j++) {
          if (mGame.enemies[j].alive) {
            INT16 dx = (INT16)(mGame.enemies[j].x - spawnX);
            INT16 dy = (INT16)(mGame.enemies[j].y - spawnY);
            if (dx < TANK_PX_W && dx > -TANK_PX_W && dy < TANK_PX_H && dy > -TANK_PX_H) { blocked = TRUE; break; }
          }
        }
        if (blocked) continue;
      }
      if (TankCollides(spawnX, spawnY, FALSE)) continue;

      mGame.enemies[i].x = spawnX;
      mGame.enemies[i].y = spawnY;
      mGame.enemies[i].dir = DIR_DOWN;
      mGame.enemies[i].speed = 1;
      mGame.enemies[i].alive = TRUE;
      mGame.enemies[i].type = (UINT8)(Rand() % 4);
      mGame.enemies[i].shootCd = 20 + (UINT8)(Rand() % 60);
      mGame.enemies[i].invincible = 30;
      mGame.enemies[i].upgrade = (mGame.enemies[i].type == 3) ? 1 : 0; // green=fast bullet
      mGame.enemies[i].color = 0;
      SpawnDebris(spawnX + TANK_PX_W/2, spawnY + TANK_PX_H/2, COLOR_LTGREY);
      SpawnDebris(spawnX + TANK_PX_W/2, spawnY + TANK_PX_H/2, COLOR_GREY);
      mGame.enemiesSpawned++;
      mGame.enemiesAlive++;
      mGame.spawnTimer = SPAWN_DELAY;
      break;
    }
  }
}

// === Update game logic ===
static void UpdateGame(void) {
  INT16 i, j, newX, newY;
  BOOLEAN moved;

  if (mGame.state != GS_PLAYING) return;
  if (mGame.pause) return;
  if (mGame.stageIntro > 0) { mGame.stageIntro--; return; }

  // --- Player input ---
  if (mGame.player.alive) {
    moved = FALSE;
    newX = mGame.player.x;
    newY = mGame.player.y;

    if (mKeyUp)    { mGame.player.dir = DIR_UP;    newY -= mGame.player.speed; moved = TRUE; }
    if (mKeyDown)  { mGame.player.dir = DIR_DOWN;  newY += mGame.player.speed; moved = TRUE; }
    if (mKeyLeft)  { mGame.player.dir = DIR_LEFT;  newX -= mGame.player.speed; moved = TRUE; }
    if (mKeyRight) { mGame.player.dir = DIR_RIGHT; newX += mGame.player.speed; moved = TRUE; }

    if (moved) {
      if (!TankCollides(newX, newY, TRUE)) {
        // Check collision with enemy tanks
        {
          INT16 saveX = mGame.player.x, saveY = mGame.player.y;
          BOOLEAN hit = FALSE;
          mGame.player.x = newX;
          mGame.player.y = newY;
          for (j = 0; j < MAX_ENEMIES; j++) {
            if (mGame.enemies[j].alive && TanksOverlap(&mGame.player, &mGame.enemies[j]))
              { hit = TRUE; break; }
          }
          if (!hit) { /* keep new position */ }
          else { mGame.player.x = saveX; mGame.player.y = saveY; }
        }
      }
    }

    if (mGame.player.shootCd > 0) mGame.player.shootCd--;
    if (mKeyFire && mGame.player.shootCd == 0) {
      FireBullet(&mGame.player, TRUE);
    }
    if (mGame.player.invincible > 0) mGame.player.invincible--;
  }

  // --- Enemy AI ---
  for (i = 0; i < MAX_ENEMIES; i++) {
    Tank *e = &mGame.enemies[i];
    if (!e->alive) continue;
    if (e->invincible > 0) e->invincible--;
    if (e->shootCd > 0) e->shootCd--;

    // Movement (with random direction changes)
    if (Rand() % 80 == 0) {
      e->dir = (UINT8)(Rand() % 4);
    }

    // Speed: fast (type1) moves 2px every frame, others 1px every 2nd frame
    {
      INT16 step;
      if (e->type == 1) { step = 2; }                    // fast: 4x speed
      else if ((mGame.frame % ENEMY_MOVE_EVERY) != 0) continue;
      else { step = 1; }                                   // normal: 1x

      newX = e->x;
      newY = e->y;
      switch (e->dir) {
        case DIR_UP:    newY -= step; break;
        case DIR_DOWN:  newY += step; break;
        case DIR_LEFT:  newX -= step; break;
        case DIR_RIGHT: newX += step; break;
      }
    }

    if (TankCollides(newX, newY, TRUE)) {
      e->dir = (UINT8)(Rand() % 4);  // Hit wall, change direction
    } else {
      // Check tank-tank collision
      BOOLEAN blocked = FALSE;
      INT16 saveX = e->x, saveY = e->y;
      e->x = newX;
      e->y = newY;

      // Check vs player
      if (mGame.player.alive && TanksOverlap(e, &mGame.player))
        blocked = TRUE;
      // Check vs other enemies
      for (j = 0; j < MAX_ENEMIES && !blocked; j++) {
        if (i != j && mGame.enemies[j].alive && TanksOverlap(e, &mGame.enemies[j]))
          blocked = TRUE;
      }

      if (blocked) {
        e->x = saveX; e->y = saveY;
        e->dir = (UINT8)(Rand() % 4);
      }
    }

    // Shooting
    if (e->shootCd == 0 && Rand() % 40 == 0) {
      FireBullet(e, FALSE);
    }
  }

  // --- Enemy spawning ---
  if (mGame.enemiesSpawned < mGame.map.enemyTotal && mGame.enemiesAlive < MAX_ENEMIES) {
    if (mGame.spawnTimer > 0) mGame.spawnTimer--;
    else SpawnEnemy();
  }

  // --- Update bullets ---
  for (i = 0; i < MAX_BULLETS; i++) {
    Bullet *b = &mGame.bullets[i];
    if (!b->alive) continue;

    switch (b->dir) {
      case DIR_UP:    b->y -= b->speed; break;
      case DIR_DOWN:  b->y += b->speed; break;
      case DIR_LEFT:  b->x -= b->speed; break;
      case DIR_RIGHT: b->x += b->speed; break;
    }

    // Out of bounds
    if (b->x < -16 || b->x > MAP_PX_W + 16 || b->y < -16 || b->y > MAP_PX_H + 16) {
      b->alive = FALSE; continue;
    }

    // Tile collision
    {
      UINT8 hitTile = BulletHitsTile(b);
      if (hitTile == TILE_BASE) {
        // Base destroyed = game over
        b->alive = FALSE;
        SpawnExplosion(b->x, b->y);
        for (j = 0; j < MAP_COLS; j++)
          if (mGame.map.tiles[24][j] == TILE_BASE) mGame.map.tiles[24][j] = TILE_EMPTY;
          if (mGame.map.tiles[25][j] == TILE_BASE) mGame.map.tiles[25][j] = TILE_EMPTY;
        mGame.player.alive = FALSE;
        mGame.state = GS_GAME_OVER;
      } else if (hitTile != TILE_EMPTY) {
        if (hitTile == TILE_BRICK) {
          DestroyBricks(b->x, b->y);
          SpawnExplosion(b->x, b->y);
        }
        b->alive = FALSE;
      }
    }

    if (!b->alive) continue;

    // Bullet-vs-bullet collision (opposing bullets destroy each other)
    {
      INT16 k;
      for (k = 0; k < MAX_BULLETS; k++) {
        Bullet *b2 = &mGame.bullets[k];
        if (!b2->alive || b2 == b) continue;
        if (b2->fromPlayer == b->fromPlayer) continue;
        if (b->x - 4 < b2->x + 4 && b->x + 4 > b2->x - 4 &&
            b->y - 4 < b2->y + 4 && b->y + 4 > b2->y - 4) {
          b->alive = FALSE;
          b2->alive = FALSE;
          SpawnExplosion(b->x, b->y);
          SpawnDebris(b->x, b->y, COLOR_WHITE);
          SpawnDebris(b->x, b->y, COLOR_YELLOW);
          break;
        }
      }
    }
    if (!b->alive) continue;

    // Hit player tank?
    if (!b->fromPlayer && mGame.player.alive && mGame.player.invincible == 0) {
      INT16 px = mGame.player.x, py = mGame.player.y;
      if (b->x >= px - 2 && b->x <= px + TANK_PX_W + 2 &&
          b->y >= py - 2 && b->y <= py + TANK_PX_H + 2) {
        b->alive = FALSE;
        SpawnExplosion(mGame.player.x + TANK_PX_W/2,
                       mGame.player.y + TANK_PX_H/2);
        SpawnDebris(mGame.player.x + TANK_PX_W/2, mGame.player.y + TANK_PX_H/2, COLOR_GOLD);
        SpawnDebris(mGame.player.x + TANK_PX_W/2, mGame.player.y + TANK_PX_H/2, COLOR_DKGOLD);
        mGame.player.alive = FALSE;
        mGame.lives--;
        if (mGame.lives <= 0)
          mGame.state = GS_GAME_OVER;
        else
          mGame.player.respawn = 60; // We'll handle respawn
      }
    }

    // Hit enemy tanks?
    if (b->fromPlayer) {
      for (j = 0; j < MAX_ENEMIES; j++) {
        Tank *e = &mGame.enemies[j];
        if (!e->alive) continue;
        if (e->invincible > 0) continue;
        {
          INT16 ex = e->x, ey = e->y;
          if (b->x >= ex - 2 && b->x <= ex + TANK_PX_W + 2 &&
              b->y >= ey - 2 && b->y <= ey + TANK_PX_H + 2) {
            b->alive = FALSE;
            e->alive = FALSE;
            mGame.enemiesAlive--;
            mGame.score += 100;
            SpawnExplosion(e->x + TANK_PX_W/2,
                           e->y + TANK_PX_H/2);
            SpawnDebris(e->x + TANK_PX_W/2, e->y + TANK_PX_H/2, COLOR_LTGREY);
            SpawnDebris(e->x + TANK_PX_W/2, e->y + TANK_PX_H/2, COLOR_DARKGREY);
            if (Rand() % 2 == 0)
              SpawnPowerUp(e->x, e->y);
            // Check stage clear
            if (mGame.enemiesAlive == 0 && mGame.enemiesSpawned >= mGame.map.enemyTotal) {
              mGame.state = GS_STAGE_CLEAR;
              mGame.stageIntro = 60;
            }
            break;
          }
        }
      }
    }
  }

  // --- Player respawn ---
  if (!mGame.player.alive && mGame.state == GS_PLAYING && mGame.lives > 0 && mGame.player.respawn > 0) {
    mGame.player.respawn--;
    if (mGame.player.respawn == 0) {
      mGame.player.x = 12 * TILE_SIZE;
      mGame.player.y = 22 * TILE_SIZE;
      mGame.player.dir = DIR_UP;
      mGame.player.alive = TRUE;
      mGame.player.invincible = 180;
      SpawnDebris(mGame.player.x + TANK_PX_W/2, mGame.player.y + TANK_PX_H/2, COLOR_GOLD);
      SpawnDebris(mGame.player.x + TANK_PX_W/2, mGame.player.y + TANK_PX_H/2, COLOR_YELLOW);
      mGame.player.upgrade = 0;
    }
  }

  // --- Update power-ups ---
  for (i = 0; i < MAX_POWERUPS; i++) {
    PowerUp *p = &mGame.powerUps[i];
    if (!p->alive) continue;
    if (p->timer > 0) p->timer--;
    else { p->alive = FALSE; continue; }

    // Player pickup?
    if (mGame.player.alive) {
      INT16 ppx = mGame.player.x, ppy = mGame.player.y;
      if (ppx < p->x + 20 && ppx + TANK_PX_W > p->x - 4 &&
          ppy < p->y + 20 && ppy + TANK_PX_H > p->y - 4) {
        switch (p->type) {
          case 0: mGame.player.upgrade = MIN(mGame.player.upgrade + 1, 2); break; // Star
          case 1: mGame.lives = MIN(mGame.lives + 1, 9); break; // Extra life
          case 2: mGame.player.invincible = INVINCIBLE_TIME; break; // Shield
          case 3: /* Time stop - freeze enemies */ break; // Clock
          case 4: // Bomb - destroy all enemies
            for (j = 0; j < MAX_ENEMIES; j++) {
              if (mGame.enemies[j].alive) {
                mGame.enemies[j].alive = FALSE;
                mGame.enemiesAlive--;
                mGame.score += 100;
                SpawnExplosion(mGame.enemies[j].x + TANK_PX_W/2,
                               mGame.enemies[j].y + TANK_PX_H/2);
                SpawnDebris(mGame.enemies[j].x + TANK_PX_W/2, mGame.enemies[j].y + TANK_PX_H/2, COLOR_LTGREY);
              }
            }
            // Check stage clear after bomb
            if (mGame.enemiesAlive == 0 && mGame.enemiesSpawned >= mGame.map.enemyTotal) {
              mGame.state = GS_STAGE_CLEAR;
              mGame.stageIntro = 60;
            }
            break;
        }
        p->alive = FALSE;
      }
    }
  }

  // --- Update debris ---
  UpdateDebris();

  // --- Update explosions ---
  for (i = 0; i < MAX_EXPLOSIONS; i++) {
    if (mGame.explosions[i].alive) {
      if (mGame.explosions[i].timer > 0)
        mGame.explosions[i].timer--;
      else
        mGame.explosions[i].alive = FALSE;
    }
  }
}

// === Draw the map ===
static void DrawMap(void) {
  INT16 r, c;
  for (r = 0; r < MAP_ROWS; r++) {
    for (c = 0; c < MAP_COLS; c++) {
      UINT8 t = mGame.map.tiles[r][c];
      INT16 px = MAP_X + c * TILE_SIZE;
      INT16 py = MAP_Y + r * TILE_SIZE;
      switch (t) {
        case TILE_BRICK: DrawSprite(px, py, sprBrick); break;
        case TILE_STEEL: DrawSprite(px, py, sprSteel); break;
        case TILE_WATER: DrawSprite(px, py, sprWater); break;
        case TILE_TREE:  DrawSprite(px, py, sprTree);  break;
        case TILE_BASE:  DrawSprite(px, py, sprBase);  break;
        default: FillRect(px, py, TILE_SIZE, TILE_SIZE, COLOR_DARKGREY); break;
      }
    }
  }
}

// === Draw HUD (right panel) ===
static void DrawHUD(void) {
  INT16 x = HUD_X, y = HUD_Y;
  INT16 i;

  // Panel background
  FillRect(x - 8, 0, 192, SCREEN_H, COLOR_BLACK);
  FillRect(x - 8, 0, 2, SCREEN_H, COLOR_GOLD);

  // Title: 坦克大战
  FillRect(x, y, 160, 24, COLOR_DARKGREY);
  DrawText(x + 32, y + 2, L"坦克大战", COLOR_GOLD, FALSE);
  y += 32;

  // Stage: 第X关
  FillRect(x, y, 160, 20, COLOR_DKBLUE);
  {
    CHAR16 buf[8];
    FormatStage(buf, mGame.map.stage);
    DrawText(x + 8, y + 2, buf, COLOR_YELLOW, FALSE);
  }
  y += 26;

  // Lives: 生命 X
  FillRect(x, y, 160, 20, COLOR_DKBLUE);
  DrawText(x + 4, y + 2, L"生命", COLOR_WHITE, FALSE);
  for (i = 0; i < mGame.lives; i++) {
    INT16 ix = x + 44 + i * 24;
    FillRect(ix, y + 3, 14, 14, COLOR_GOLD);
    FillRect(ix - 2, y + 1, 4, 18, COLOR_DKGOLD);
    FillRect(ix + 12, y + 1, 4, 18, COLOR_DKGOLD);
  }
  y += 26;

  // Enemies: 敌军
  FillRect(x, y, 160, 20, COLOR_DKBLUE);
  DrawText(x + 4, y + 2, L"敌军", COLOR_WHITE, FALSE);
  {
    INT16 remaining = mGame.map.enemyTotal - mGame.enemiesSpawned + mGame.enemiesAlive;
    if (remaining < 0) remaining = 0;
    for (i = 0; i < remaining && i < 10; i++) {
      INT16 ix = x + 44 + i * 12;
      FillRect(ix, y + 5, 8, 10, COLOR_LTGREY);
      FillRect(ix - 1, y + 4, 2, 12, COLOR_DARKGREY);
      FillRect(ix + 7, y + 4, 2, 12, COLOR_DARKGREY);
    }
  }
  y += 26;

  // Score: 得分
  FillRect(x, y, 160, 20, COLOR_DKBLUE);
  DrawText(x + 4, y + 2, L"得分", COLOR_WHITE, FALSE);
  {
    UINT32 sc = mGame.score;
    INT16 barW = (INT16)(sc / 50);
    if (barW > 100) barW = 100;
    if (barW > 0) FillRect(x + 44, y + 5, barW, 10, COLOR_GOLD);
  }
  y += 26;

  // Power-up status
  if (mGame.player.alive) {
    if (mGame.player.upgrade > 0) {
      FillRect(x, y, 160, 18, COLOR_DARKGREY);
      DrawText(x + 2, y + 2, L"升级", COLOR_YELLOW, FALSE);
      y += 20;
    }
    if (mGame.player.invincible > 0) {
      FillRect(x, y, 160, 18, COLOR_DARKGREY);
      DrawText(x + 2, y + 2, L"无敌", COLOR_BLUE, FALSE);
      FillRect(x + 42, y + 5, mGame.player.invincible / 3, 8, COLOR_BLUE);
      y += 20;
    }
  }
  // Version at bottom of panel
  {
    INT16 vy = SCREEN_H - 20;
    FillRect(x - 8, vy, 192, 20, COLOR_DARKGREY);
    DrawText(x + 4, vy + 2, L"坦克大战", COLOR_GOLD, FALSE);
    DrawText(x + 68, vy + 2, L"0.6", COLOR_LTGREY, FALSE);
  }
}

// === Draw title screen ===
static void DrawTitle(void) {
  INT16 la_w = MAP_X + MAP_PX_W; // left area full width (432)
  INT16 lcx = la_w / 2;           // left area center X
  INT16 lcy = SCREEN_H / 2;       // center Y (240)

  // Left area: fill to edge, no margin
  FillRect(0, 0, SCREEN_W, SCREEN_H, COLOR_BLACK);
  FillRect(0, 0, la_w, SCREEN_H, COLOR_DARKGREY);

  // Grid pattern across full left area
  {
    INT16 gx, gy;
    for (gx = TILE_SIZE; gx < la_w; gx += TILE_SIZE * 4)
      FillRect(gx, 0, 1, SCREEN_H, COLOR_GREY);
    for (gy = TILE_SIZE; gy < SCREEN_H; gy += TILE_SIZE * 4)
      FillRect(0, gy, la_w, 1, COLOR_GREY);
  }

  // Gold divider at right edge of left area
  FillRect(la_w, 0, 2, SCREEN_H, COLOR_GOLD);

  // === LEFT: Centered title content ===
  FillRect(lcx - 100, lcy - 70, 200, 40, COLOR_BLACK);
  DrawText(lcx - 64, lcy - 64, L"坦克大战", COLOR_GOLD, TRUE);
  DrawText(lcx - 40, lcy - 28, L"一九九零", COLOR_LTGREY, FALSE);

  // Three demo tanks
  DrawTankProc(lcx - 16,  lcy + 20, DIR_UP,   COLOR_GOLD,   COLOR_DKGOLD, COLOR_DARKGREY, FALSE);
  DrawTankProc(lcx - 80,  lcy + 20, DIR_DOWN, COLOR_LTGREY, COLOR_GREY,   COLOR_DARKGREY, FALSE);
  DrawTankProc(lcx + 48,  lcy + 20, DIR_LEFT, COLOR_RED,    COLOR_DKRED,  COLOR_DARKGREY, FALSE);

  if (mGame.titleBlink < 40) {
    DrawText(lcx - 64, lcy + 90, L"按空格开始", COLOR_GREEN, FALSE);
  }

  // === RIGHT: Structured panel ===
  {
    INT16 rx = HUD_X, ry = 20;
    INT16 lx = rx + 6;   // left column (labels)
    INT16 dx = rx + 58;  // right column (descriptions)

    // Header block
    FillRect(rx - 4, ry, 160, 26, COLOR_DKBLUE);
    DrawText(lx, ry + 3, L"坦克大战经典版", COLOR_GOLD, FALSE);
    ry += 30;

    // Version
    DrawText(lx, ry, L"版本", COLOR_LTGREY, FALSE);
    DrawText(dx, ry, L"0.6", COLOR_LTGREY, FALSE);
    ry += 20;

    // --- Controls ---
    FillRect(rx - 4, ry, 160, 20, COLOR_DKBLUE);
    DrawText(lx, ry + 2, L"操作说明", COLOR_WHITE, FALSE);
    ry += 24;

    DrawText(lx, ry, L"方向键", COLOR_LTGREY, FALSE);
    DrawText(dx, ry, L"移动坦克", COLOR_LTGREY, FALSE);
    ry += 16;
    DrawText(lx, ry, L"空格键", COLOR_LTGREY, FALSE);
    DrawText(dx, ry, L"发射炮弹", COLOR_LTGREY, FALSE);
    ry += 16;
    DrawText(lx, ry, L"ESC键", COLOR_LTGREY, FALSE);
    DrawText(dx, ry, L"退出游戏", COLOR_LTGREY, FALSE);
    ry += 20;

    // --- Power-ups ---
    FillRect(rx - 4, ry, 160, 20, COLOR_DKBLUE);
    DrawText(lx, ry + 2, L"道具说明", COLOR_WHITE, FALSE);
    ry += 24;

    // Icon column (at lx) + effect column (at dx)
    FillRect(lx,  ry + 4, 10, 10, COLOR_GOLD);
    DrawText(lx + 14, ry, L"星", COLOR_GOLD, FALSE);
    DrawText(dx, ry, L"武器升级穿甲", COLOR_LTGREY, FALSE);
    ry += 16;
    FillRect(lx,  ry + 4, 10, 10, COLOR_GREEN);
    DrawText(lx + 14, ry, L"命", COLOR_GREEN, FALSE);
    DrawText(dx, ry, L"增加一条生命", COLOR_LTGREY, FALSE);
    ry += 16;
    FillRect(lx,  ry + 4, 10, 10, COLOR_BLUE);
    DrawText(lx + 14, ry, L"护", COLOR_BLUE, FALSE);
    DrawText(dx, ry, L"无敌护盾", COLOR_LTGREY, FALSE);
    ry += 16;
    FillRect(lx,  ry + 4, 10, 10, COLOR_LTGREY);
    DrawText(lx + 14, ry, L"停", COLOR_LTGREY, FALSE);
    DrawText(dx, ry, L"冻结敌军", COLOR_LTGREY, FALSE);
    ry += 16;
    FillRect(lx,  ry + 4, 10, 10, COLOR_RED);
    DrawText(lx + 14, ry, L"炸", COLOR_RED, FALSE);
    DrawText(dx, ry, L"清除敌军", COLOR_LTGREY, FALSE);
    ry += 22;

    // --- Enemy types ---
    FillRect(rx - 4, ry, 160, 20, COLOR_DKBLUE);
    DrawText(lx, ry + 2, L"敌军类型", COLOR_WHITE, FALSE);
    ry += 24;

    FillRect(lx,  ry + 4, 10, 10, COLOR_LTGREY);
    DrawText(lx + 14, ry, L"银", COLOR_LTGREY, FALSE);
    DrawText(dx, ry, L"基础型", COLOR_LTGREY, FALSE);
    ry += 16;
    FillRect(lx,  ry + 4, 10, 10, COLOR_RED);
    DrawText(lx + 14, ry, L"红", COLOR_RED, FALSE);
    DrawText(dx, ry, L"快速型", COLOR_LTGREY, FALSE);
    ry += 16;
    FillRect(lx,  ry + 4, 10, 10, 0x000080C0);
    DrawText(lx + 14, ry, L"蓝", 0x000080C0, FALSE);
    DrawText(dx, ry, L"重型", COLOR_LTGREY, FALSE);
    ry += 16;
    FillRect(lx,  ry + 4, 10, 10, COLOR_GREEN);
    DrawText(lx + 14, ry, L"绿", COLOR_GREEN, FALSE);
    DrawText(dx, ry, L"速射型", COLOR_LTGREY, FALSE);
    ry += 24;

    // Decorative tank
    DrawTankProc(rx + 34, ry, DIR_RIGHT, COLOR_GOLD, COLOR_DKGOLD, COLOR_DARKGREY, FALSE);
  }
}

// === Draw stage intro overlay ===
static void DrawStageIntro(void) {
  if (mGame.stageIntro <= 0) return;
  INT16 cx = SCREEN_W / 2, cy = SCREEN_H / 2;
  FillRect(cx - 120, cy - 30, 240, 60, COLOR_BLACK);
  FillRect(cx - 116, cy - 26, 232, 52, COLOR_GOLD);
  FillRect(cx - 112, cy - 22, 224, 44, COLOR_BLACK);

  {
    CHAR16 buf[8];
    FormatStage(buf, mGame.map.stage);
    DrawText(cx - 48, cy - 10, buf, COLOR_WHITE, TRUE);
  }
}

// === Render one frame ===
static void RenderFrame(void) {
  FillRect(0, 0, SCREEN_W, SCREEN_H, COLOR_BLACK);

  if (mGame.state == GS_TITLE) {
    DrawTitle();
  } else {
    DrawMap();
    DrawHUD();

    // Draw entities
    {
      INT16 i;
      // Power-ups
      for (i = 0; i < MAX_POWERUPS; i++)
        if (mGame.powerUps[i].alive) DrawPowerUpGame(&mGame.powerUps[i]);
      // Bullets
      for (i = 0; i < MAX_BULLETS; i++)
        if (mGame.bullets[i].alive) DrawBulletGame(&mGame.bullets[i]);
      // Player
      if (mGame.player.alive) DrawTankGame(&mGame.player, TRUE);
      // Enemies
      for (i = 0; i < MAX_ENEMIES; i++)
        if (mGame.enemies[i].alive) DrawTankGame(&mGame.enemies[i], FALSE);
      // Explosions
      for (i = 0; i < MAX_EXPLOSIONS; i++)
        if (mGame.explosions[i].alive) DrawExplosionGame(&mGame.explosions[i]);
      // Debris
      for (i = 0; i < MAX_DEBRIS; i++)
        if (mGame.debris[i].alive) DrawDebrisGame(&mGame.debris[i]);
    }

    // Tree overlay (trees should cover tanks)
    {
      INT16 r, c;
      for (r = 0; r < MAP_ROWS; r++)
        for (c = 0; c < MAP_COLS; c++)
          if (mGame.map.tiles[r][c] == TILE_TREE)
            DrawTileSprite(c, r, sprTree);
    }

    if (mGame.stageIntro > 0 && mGame.state == GS_PLAYING)
      DrawStageIntro();

    // Game over / Stage clear overlay
    if (mGame.state == GS_GAME_OVER) {
      INT16 cx = SCREEN_W / 2, cy = SCREEN_H / 2;
      FillRect(cx - 120, cy - 25, 240, 50, COLOR_BLACK);
      FillRect(cx - 116, cy - 21, 232, 42, COLOR_GOLD);
      FillRect(cx - 112, cy - 17, 224, 34, COLOR_BLACK);
      DrawText(cx - 80, cy - 12, L"游戏结束", COLOR_RED, TRUE);
    }

    if (mGame.state == GS_STAGE_CLEAR) {
      INT16 cx = SCREEN_W/2, cy = SCREEN_H/2;
      // Pulsing glow
      INT16 gs = (mGame.stageIntro % 20);
      INT16 gw = 220 + gs * 2, gh = 70 + gs;
      FillRect(cx - gw/2, cy - gh/2, gw, gh, COLOR_DARKGREY);
      // Gold border
      FillRect(cx - 110, cy - 30, 220, 60, COLOR_GOLD);
      FillRect(cx - 106, cy - 26, 212, 52, COLOR_BLACK);
      // Decorative corners
      FillRect(cx - 100, cy - 24, 12, 3, COLOR_YELLOW);
      FillRect(cx - 100, cy - 24, 3, 12, COLOR_YELLOW);
      FillRect(cx + 88, cy - 24, 12, 3, COLOR_YELLOW);
      FillRect(cx + 97, cy - 24, 3, 12, COLOR_YELLOW);
      FillRect(cx - 100, cy + 21, 12, 3, COLOR_YELLOW);
      FillRect(cx - 100, cy + 12, 3, 12, COLOR_YELLOW);
      FillRect(cx + 88, cy + 21, 12, 3, COLOR_YELLOW);
      FillRect(cx + 97, cy + 12, 3, 12, COLOR_YELLOW);
      // Title
      DrawText(cx - 48, cy - 16, L"通关", COLOR_GOLD, TRUE);
      {
        CHAR16 buf[8];
        FormatStage(buf, mGame.map.stage);
        DrawText(cx - 24, cy + 18, buf, COLOR_GREEN, FALSE);
      }
    }

    if (mGame.state == GS_VICTORY) {
      INT16 cx = SCREEN_W/2, cy = SCREEN_H/2;
      FillRect(cx - 120, cy - 25, 240, 50, COLOR_BLACK);
      FillRect(cx - 116, cy - 21, 232, 42, COLOR_GOLD);
      FillRect(cx - 112, cy - 17, 224, 34, COLOR_BLACK);
      DrawText(cx - 80, cy - 12, L"胜利", COLOR_GOLD, TRUE);
    }

    if (mGame.state == GS_PLAYING && mGame.stageIntro > 0)
      DrawStageIntro();
  }
}

// === Main entry ===
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status;
  UINTN Mode, InfoSize, BestMode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  UINTN Index;
  EFI_INPUT_KEY Key;
  UINT32 dbgFrame;

  Print(L"TANK: Starting Tank Battle 1990...\n");

  // Locate GOP
  Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mGop);
  if (EFI_ERROR(Status)) {
    Print(L"TANK: ERROR - GOP not found: %r\n", Status);
    return Status;
  }
  Print(L"TANK: GOP located, MaxMode=%d, current Mode=%d\n",
        mGop->Mode->MaxMode, mGop->Mode->Mode);

  // Find 640x480 mode
  BestMode = mGop->Mode->Mode;
  for (Mode = 0; Mode < mGop->Mode->MaxMode; Mode++) {
    Status = mGop->QueryMode(mGop, Mode, &InfoSize, &Info);
    if (EFI_ERROR(Status)) continue;
    if (Info->HorizontalResolution == 640 && Info->VerticalResolution == 480) {
      BestMode = Mode;
      break;
    }
  }

  Status = mGop->SetMode(mGop, BestMode);
  if (EFI_ERROR(Status)) {
    Print(L"TANK: SetMode(%d) failed: %r\n", BestMode, Status);
  }

  mFrameW = mGop->Mode->Info->HorizontalResolution;
  mFrameH = mGop->Mode->Info->VerticalResolution;
  Print(L"TANK: v0.6 - 640x480 GOP ready\n", mFrameW, mFrameH);

  mBackBuf = (UINT32 *)AllocatePool(mFrameW * mFrameH * sizeof(UINT32));
  if (mBackBuf == NULL) {
    Print(L"TANK: ERROR allocating back buffer\n");
    return EFI_OUT_OF_RESOURCES;
  }

  SetMem(&mGame, sizeof(mGame), 0);
  ResetGame();
  Print(L"TANK: Game started! Press Space to begin.\n");

  dbgFrame = 0;
  while (TRUE) {
    // Poll input (non-blocking)
    mKeyUp = mKeyDown = mKeyLeft = mKeyRight = mKeyFire = mKeyStart = FALSE;

    while (gST->ConIn->ReadKeyStroke(gST->ConIn, &Key) == EFI_SUCCESS) {
      if (Key.ScanCode == 0x0001) mKeyUp = TRUE;
      else if (Key.ScanCode == 0x0002) mKeyDown = TRUE;
      else if (Key.ScanCode == 0x0004) mKeyLeft = TRUE;
      else if (Key.ScanCode == 0x0003) mKeyRight = TRUE;
      else if (Key.UnicodeChar == ' ') mKeyFire = TRUE;
      else if (Key.UnicodeChar == 0x000d || Key.UnicodeChar == 0x000a) mKeyStart = TRUE;
      else if (Key.ScanCode == 0x0017) { // ESC -> exit
        FreePool(mBackBuf);
        return EFI_SUCCESS;
      }
    }

    // Handle title/end screens
    mGame.titleBlink++;
    if (mGame.titleBlink >= 80) mGame.titleBlink = 0;

    if (mGame.state == GS_TITLE) {
      if (mKeyStart) {
        mGame.lives = 3;
        mGame.score = 0;
        InitGame(1);
      }
    } else if (mGame.state == GS_GAME_OVER) {
      DrawMap();
      mGame.pause = 1;
      if (mGame.stageIntro > 0) mGame.stageIntro--;
      if (mGame.stageIntro == 0 && mKeyStart) {
        mGame.lives = 3;
        mGame.score = 0;
        InitGame(1);
      }
    } else if (mGame.state == GS_STAGE_CLEAR) {
      if (mGame.stageIntro > 0) {
        mGame.stageIntro--;
      } else if (mKeyStart) {
        INT16 nextStage = mGame.map.stage + 1;
        if (nextStage > 35) {
          mGame.state = GS_VICTORY;
          mGame.stageIntro = 300;
        } else {
          mGame.lives = MIN(mGame.lives + 1, 9);
          InitGame(nextStage);
        }
      }
    } else if (mGame.state == GS_VICTORY) {
      if (mGame.stageIntro > 0) {
        mGame.stageIntro--;
      } else if (mKeyStart) {
        ResetGame();
      }
    } else if (mGame.state == GS_PLAYING) {
      UpdateGame();
    }

    // Render
    RenderFrame();

    // Blt back buffer to screen
    Status = mGop->Blt(mGop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)mBackBuf,
              EfiBltBufferToVideo, 0, 0, 0, 0,
              mFrameW, mFrameH, 0);
    if (EFI_ERROR(Status) && dbgFrame == 0) {
      Print(L"TANK: Blt error: %r\n", Status);
    }

    // Frame limiter
    gBS->Stall(FRAME_US);
    mGame.frame++;
    dbgFrame++;
  }

  FreePool(mBackBuf);
  return EFI_SUCCESS;
}
