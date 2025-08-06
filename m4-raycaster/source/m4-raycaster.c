#include "tonc_input.h"
#include "tonc_math.h"
#include "tonc_tte.h"
#include "tonc_video.h"
#include <stdlib.h>


// Fixed-point math
enum FixedShiftConsts {
    FIXED_SHIFT = 12,
    FIXED_HALF = 1 << (FIXED_SHIFT - 1),
};

// Convert to Fixed point for macros
#define INT_TO_FIXED(x) ((int)((x) << FIXED_SHIFT))

enum MathConsts {
    LU_PI = 0x8000,
};

enum TimeConsts {
    SYSCLK_64 = 262144,
};

enum MapConsts {
    TILE_SIZE = 8,
    MAP_WIDTH = 8,
    MAP_HEIGHT = 8,
    MAP_X = 80,
    MAP_Y = 40,
};

enum ColorConsts {
    BLACK_COLOR_IDX = 0,
    DIR_COLOR_IDX = 1,
    PLAYER_COLOR_IDX = 2,
    FLOOR_COLOR_IDX = 3,
    WALL_COLOR_IDX = 4,
};


enum PlayerConsts {
    FOV = LU_PI/2,
    RAY_LENGTH = 30,
    LINEAR_SPEED = 5,
    ANGULAR_SPEED = LU_PI/3000,
    PLAYER_START_X = INT_TO_FIXED(MAP_X+1*TILE_SIZE) + INT_TO_FIXED(TILE_SIZE/2),
    PLAYER_START_Y = INT_TO_FIXED(MAP_Y+6*TILE_SIZE) + INT_TO_FIXED(TILE_SIZE/2),
    PLAYER_START_THETA = INT_TO_FIXED(0),
};


static const u16 worldMap[MAP_HEIGHT][MAP_WIDTH] = {
    {1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 1, 0, 1, 1},
    {1, 0, 1, 0, 1, 0, 0, 1},
    {1, 0, 0, 0, 1, 1, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}
};

// Convert to Fixed point
static inline s32 int_to_fixed(s32 x) {
    return x << FIXED_SHIFT;
}

// Convert to integer. It adds half the divisor to round up
static inline s32 fixed_to_int(s32 x) {
    const s32 half = 1 << (FIXED_SHIFT - 1);
    if (x >= 0) {
        return (x + half) >> FIXED_SHIFT;
    }
    else {
        return (x - half) >> FIXED_SHIFT;
    }
}

// Player position
static u32 playerX = PLAYER_START_X;
static u32 playerY = PLAYER_START_Y;

// Player rotation
static u32 playerTheta = PLAYER_START_THETA;

// Time
static u32 lastTicks;
static u32 fps;
static u16 dt;

static inline u8* back_page(void) {
    return (u8*)0x06000000
         + ((REG_DISPCNT & DCNT_PAGE) ? 0x0000 : 0xA000);
}

static inline s32 fixed_mul(s32 a, s32 b) {
    return (s32)(((s64)a * b) >> FIXED_SHIFT);
}


int pixel_in_collision(u32 x, u32 y){
    int playerXTile = (x-MAP_X)/TILE_SIZE;
    int playerYTile = (y-MAP_Y)/TILE_SIZE;
    return worldMap[playerYTile][playerXTile];
}

void render_direction(u8 color) {
    // distance, no collisions
    m4_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, BLACK_COLOR_IDX);
    u32 resolution = FOV/40;
    for (s16 i = -FOV/2; i < FOV/2+1; i = i + resolution) {
        s32 dist = RAY_LENGTH;
        s32 xDir = lu_cos(playerTheta + i);
        s32 yDir = lu_sin(playerTheta + i);
        for (u32 j = 1; j < RAY_LENGTH + 1; j++) {
            u32 xRay = playerX+j*xDir;
            u32 yRay = playerY+j*yDir;
            if (pixel_in_collision(fixed_to_int(xRay), fixed_to_int(yRay))) {
                dist = j;
                break;
            }
        }
        // If wall within range
        s32 lineHeight = SCREEN_HEIGHT/dist;
        s32 offset = SCREEN_HEIGHT/2 - lineHeight/2;
        u32 num_rays = FOV/resolution;
        u32 sliceW = SCREEN_WIDTH/num_rays;
        u32 rayIndex = (i + FOV/2)/resolution;
        u32 leftX = rayIndex * sliceW;
        // Draw floor
        m4_rect(leftX, offset + lineHeight, leftX + sliceW, SCREEN_HEIGHT, FLOOR_COLOR_IDX);
        // Draw walls
        if (dist < RAY_LENGTH) {
            m4_rect(leftX, offset, leftX + sliceW, offset + lineHeight, color);
        }
        tte_write("#{P:50,145}");
        tte_erase_line();
        tte_printf("dist: %d", dist);
    }
}

static inline s16 clamp_steps(
    u32 currentAxisCoord,
    s32 delta,
    u32 otherAxisCoord,
    bool isVertical
    ) 
{
    if (delta == 0) return 0;
    s32  sign  = (delta > 0) ?  1 : -1;
    u32  steps = abs(delta);
    for (u16 i = 1; i <= steps; i = i + TILE_SIZE) {
        u32 x = isVertical ? otherAxisCoord : currentAxisCoord + sign * i;
        u32 y = isVertical ? currentAxisCoord + sign * i : otherAxisCoord;
        if (pixel_in_collision(fixed_to_int(x), fixed_to_int(y)))
            return sign * (i - TILE_SIZE);
    }
    return sign * steps;
}

void update_player() {
    key_poll();

    s16 moveX = 0, moveY = 0, rotateTheta = 0;

    s16 secPerFrame = int_to_fixed(dt) / SYSCLK_64;
    s16 linearMove = LINEAR_SPEED * secPerFrame;
    s16 angularMove = ANGULAR_SPEED * secPerFrame;
    if (key_is_down(KEY_UP)) moveY += linearMove;
    if (key_is_down(KEY_DOWN)) moveY += -linearMove;
    if (key_is_down(KEY_B)) moveX += linearMove;
    if (key_is_down(KEY_A)) moveX += -linearMove;
    if (key_is_down(KEY_LEFT)) rotateTheta += -angularMove;
    if (key_is_down(KEY_RIGHT)) rotateTheta += angularMove;
    // Handle moving diagonally at the same speed
    if (moveX && moveY)
    {
        moveX = moveX*0.707;
        moveY = moveY*0.707;
    }

    // Apply Rotation. No need to check for collisions in a raycaster
    playerTheta += rotateTheta;

    // Apply translation per axis
    s16 yDir = lu_sin(playerTheta);
    s16 yLatDir = lu_sin(playerTheta - LU_PI/2);
    s16 deltaY = fixed_mul(moveY, yDir) + fixed_mul(moveX, yLatDir);
    s16 safeStepsY = clamp_steps(playerY, deltaY, playerX, true);
    playerY += safeStepsY;

    s16 xDir = lu_cos(playerTheta);
    s16 xLatDir = lu_cos(playerTheta - LU_PI/2);
    s16 deltaX = fixed_mul(moveY, xDir) + fixed_mul(moveX, xLatDir);
    s16 safeStepsX = clamp_steps(playerX, deltaX, playerY, false);
    playerX += safeStepsX;

    render_direction(WALL_COLOR_IDX);
    tte_write("#{P:50,0}");
    tte_erase_line();
    tte_printf("fps: %d", fps);
}


void init_timebase(void) {
    REG_TM0CNT_L = 0;
    /* start at SYSCLK (16.78 MHz)
     * Set prescaler so that timer ticks once every 64 SYSCLK cycles (262 kHz).
     * The gba can output at most 60 fps, which means that each frame will
     * take at least 16.666 milliseconds. 
     * These are 16-Bit registers  so they will overflow when the CNT hits 65536.
     * That means that using the default /1 SYSCLK, will have the register 
     * overflowing every (1/16.7 MHz) * 65536 =  3.9 milliseconds. This is
     * shorter than 1 frame, so it's not an ideal way to count fps.
     * with /64 SYSCLK, overflow would happen at  (1/262 kHz)* 65536 = 250 ms.
     * Thus, this is the highest resolution timer that can be used to count
     * frames and calculate FPS */
    REG_TM0CNT_H = TM_ENABLE | TM_FREQ_64;
    lastTicks = REG_TM0CNT_L;
}


void calc_delta_time(void) {
    u16 now  = REG_TM0CNT_L;
    dt = now - lastTicks;
    lastTicks = now;

    /* FPS = frames/seconds = 1/(diff * 1/262144)
     * Simplifying: FPS = 262144/diff.
     * Added diff/2 to round correctly */
    fps = dt
             ? (SYSCLK_64 + dt/2) / dt
             : 0;
}


int main() {
    REG_DISPCNT = DCNT_MODE4 | DCNT_BG2;

    tte_init_bmp(DCNT_MODE4, NULL, NULL);
    tte_init_con();
    init_timebase();

    // Set up colors
    // Black background
    pal_bg_mem[BLACK_COLOR_IDX] = RGB15(0, 0, 0) | BIT(15);
    // Purple walls
    pal_bg_mem[WALL_COLOR_IDX] = RGB15(16, 0, 31) | BIT(15);
    // Green player
    pal_bg_mem[PLAYER_COLOR_IDX] = RGB15(0, 31, 0) | BIT(15);
    // Red ground
    pal_bg_mem[FLOOR_COLOR_IDX] = RGB15(16, 0, 0) | BIT(15);
    // Blue direction
    pal_bg_mem[DIR_COLOR_IDX] = RGB15(0, 0, 31) | BIT(15);

    /* 
     * Drawing the map here in both screens could make it faster, 
     * but we would have to constantly erase the player and rays' positions
     * draw_map(MAP_X, MAP_Y);
     * vid_flip();
     * draw_map(MAP_X, MAP_Y);
    */
    while (1) {
        vid_vsync();
        calc_delta_time();

        TTC *tc = tte_get_context();
        tc->dst.data  = back_page();
        tc->dst.pitch = SCREEN_WIDTH;

        update_player();
        vid_flip();
    }
}

