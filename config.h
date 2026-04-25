#ifndef _TANK_CONFIG_H_
#define _TANK_CONFIG_H_

// ============================================================
// Tank Battle (1990) - UEFI Shell Edition
// ============================================================

// --- Screen / Map ---
#define SCREEN_W        640
#define SCREEN_H        480
#define TILE_SIZE       16
#define MAP_COLS        26
#define MAP_ROWS        26
#define MAP_X           16
#define MAP_Y           32
#define MAP_PX_W        (MAP_COLS * TILE_SIZE)
#define MAP_PX_H        (MAP_ROWS * TILE_SIZE)
#define HUD_X           448
#define HUD_Y           40

// --- Tile types ---
#define TILE_EMPTY      0
#define TILE_BRICK      1
#define TILE_STEEL      2
#define TILE_WATER      3
#define TILE_TREE       4
#define TILE_BASE       5

// --- Directions ---
#define DIR_UP          0
#define DIR_DOWN        1
#define DIR_LEFT        2
#define DIR_RIGHT       3

// --- Game states ---
#define GS_TITLE        0
#define GS_PLAYING      1
#define GS_STAGE_CLEAR  2
#define GS_GAME_OVER    3
#define GS_VICTORY      4

// --- Entity limits ---
#define MAX_ENEMIES     4
#define MAX_BULLETS     16
#define MAX_POWERUPS    4
#define MAX_EXPLOSIONS  8
#define MAX_DEBRIS      32

// --- Speeds (pixels per frame, 30fps) ---
#define PLAYER_SPEED    3
#define ENEMY_MOVE_EVERY 1   // Move every frame = 30 px/s
#define BULLET_SPEED    5
#define FAST_BULLET     8

// --- Tank size in tiles ---
#define TANK_W          2
#define TANK_H          2
#define TANK_PX_W       (TANK_W * TILE_SIZE)
#define TANK_PX_H       (TANK_H * TILE_SIZE)

// --- Bullet size ---
#define BULLET_W        6
#define BULLET_H        6

// --- Timing (30fps = 33333us) ---
#define FRAME_US        33333
#define ENEMY_SHOOT_CD  30
#define PLAYER_SHOOT_CD 10
#define SPAWN_DELAY     45
#define INVINCIBLE_TIME 150
#define POWERUP_LIFE    300

// --- Colors (BGRA: Blue[7:0] Green[15:8] Red[23:16] Reserved[31:24]) ---
// In UINT32 little-endian: byte0=B, byte1=G, byte2=R, byte3=0 → hex: 0x00RRGGBB
#define COLOR_BLACK      0x00000000
#define COLOR_DARKGREY   0x00303030
#define COLOR_GREY       0x00606060
#define COLOR_LTGREY     0x00A0A0A0
#define COLOR_WHITE      0x00FFFFFF
#define COLOR_RED        0x00FF0000
#define COLOR_DKRED      0x00800000
#define COLOR_GREEN      0x0000FF00
#define COLOR_DKGREEN    0x00008000
#define COLOR_BLUE       0x000000FF
#define COLOR_DKBLUE     0x00000080
#define COLOR_YELLOW     0x00FFFF00
#define COLOR_GOLD       0x00DAA520
#define COLOR_DKGOLD     0x00B8860B
#define COLOR_BROWN      0x00A0522D
#define COLOR_DKBROWN    0x006B3410
#define COLOR_ORANGE     0x00FFA500

// Note: MIN/MAX/CLAMP are provided by EDK2 Base.h (via Uefi.h). Do not redefine.
// Note: UINT8/UINT16/UINT32/UINT64/INT8/INT16/INT32/INT64/BOOLEAN/TRUE/FALSE
// are provided by EDK2 Base.h (via Uefi.h). Do not redefine them here.

// --- Game structures ---
typedef struct { INT16 x, y; } Point;

typedef struct {
  INT16 x, y;
  UINT8 dir;
  UINT8 speed;
  UINT8 alive;
  UINT8 type;       // 0=player, 1=enemy_basic, 2=enemy_fast, 3=enemy_armored
  UINT8 shootCd;
  UINT8 invincible; // invincibility frames
  UINT8 upgrade;    // 0=normal, 1=fast bullet, 2=double damage
  UINT8 color;      // visual variant
  UINT16 respawn;   // respawn delay timer
} Tank;

typedef struct {
  INT16 x, y;
  UINT8 dir;
  UINT8 speed;
  UINT8 alive;
  UINT8 fromPlayer; // 1=player's bullet, 0=enemy's
  UINT8 piercing;   // can destroy steel
} Bullet;

typedef struct {
  INT16 x, y;
  UINT8 type;       // 0=star, 1=tank(life), 2=shield, 3=clock, 4=bomb
  UINT8 alive;
  UINT16 timer;
} PowerUp;

typedef struct {
  INT16 x, y;
  UINT8 timer;
  UINT8 alive;
} Explosion;

typedef struct {
  INT16 x, y;
  INT16 vx, vy;
  UINT8 timer;
  UINT8 alive;
  UINT32 color;
} Debris;

typedef struct {
  UINT8 tiles[MAP_ROWS][MAP_COLS];
  UINT8 enemyCount;
  UINT8 enemyTotal;
  UINT8 enemiesLeft;
  UINT8 stage;
} MapData;

typedef struct {
  Tank player;
  Tank enemies[MAX_ENEMIES];
  Bullet bullets[MAX_BULLETS];
  PowerUp powerUps[MAX_POWERUPS];
  Explosion explosions[MAX_EXPLOSIONS];
  Debris debris[MAX_DEBRIS];
  MapData map;
  UINT8 state;
  UINT8 lives;
  UINT32 score;
  UINT32 frame;
  UINT8 spawnTimer;
  UINT8 enemiesSpawned;
  UINT8 enemiesAlive;
  UINT8 pause;
  UINT8 stageIntro;
  UINT16 titleBlink;
} GameData;

#endif
