#include "tonc_input.h"
#include "tonc_math.h"
#include "tonc_memdef.h"
#include "tonc_tte.h"
#include "tonc_video.h"
#include <math.h>
#include <tonc.h>
#include <tonc_core.h>
#include <stdlib.h>

// GBA constants
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 160
#define VRAM ((volatile u16*)0x06000000)

// Math constants
// Fixed-point math
#define FIXED_SHIFT  12
// Convert to Fixed point
#define FIXED(x) ((int)((x) * (1 << FIXED_SHIFT))) 
// Convert to integer. It adds half the divisor to round up
#define FIXED_TO_INT(x) ((x + (1 << (FIXED_SHIFT - 1))) >> FIXED_SHIFT) 
#define LU_PI 0x8000

// Map Data
const u16 TILE_SIZE = 8;
// Simple 8×8 maze (1 = wall, 0 = empty space)
const u16 MAP_WIDTH = 8;
const u16 MAP_HEIGHT = 8;
const u16 MAP_X = 80;
const u16 MAP_Y = 40;
const u16 worldMap[8][8] = {
    {1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 1, 0, 1, 1},
    {1, 0, 1, 0, 1, 0, 0, 1},
    {1, 0, 0, 0, 1, 1, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}
};

// Player position
const u32 PLAYER_START_X = FIXED(MAP_X+1*TILE_SIZE) + FIXED(TILE_SIZE/2);
const u32 PLAYER_START_Y = FIXED(MAP_Y+6*TILE_SIZE) + FIXED(TILE_SIZE/2);
u32 playerX = PLAYER_START_X;
u32 playerY = PLAYER_START_Y;
u32 playerPrevX = PLAYER_START_X;
u32 playerPrevY = PLAYER_START_Y;

// Player rotation
const u16 PLAYER_START_THETA = 0;
u16 playerTheta = PLAYER_START_THETA;

// Player FOV
const u16 FOV = LU_PI/2;

// Ray length
const u16 RAY_LENGTH = 8;

// Player Speed
const s32 LINEAR_SPEED = 20;
const u16 ANGULAR_SPEED = LU_PI/3000;

// Color Palette
const u16 BLACK_COLOR_IDX = 0;
const u16 DIR_COLOR_IDX = 1;
const u16 PLAYER_COLOR_IDX = 2;
const u16 FLOOR_COLOR_IDX = 3;
const u16 WALL_COLOR_IDX = 4;

// Time
const u32 SYSCLK_64 = 262144;
const u32 SYSCLK_64_HALF = 262144/2;
static u16 lastTicks;
static u16 fps;
static u16 dt;

static inline u8* back_page(void) {
    return (u8*)0x06000000
         + ((REG_DISPCNT & DCNT_PAGE) ? 0x0000 : 0xA000);
}



int pixel_in_collision(u16 x, u16 y){
    int playerXTile = (x-MAP_X)/TILE_SIZE;
    int playerYTile = (y-MAP_Y)/TILE_SIZE;
    return worldMap[playerYTile][playerXTile];
}


void render_direction(u8 color) {
    // distance, no collisions
    m4_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, BLACK_COLOR_IDX);
    s16 resolution = FOV/10;
    for (s16 i = -FOV/2; i < FOV/2+1; i = i + resolution) {
        s16 dist = RAY_LENGTH;
        s16 xDir = lu_cos(playerTheta + i);
        s16 yDir = lu_sin(playerTheta + i);
        for (u16 j = 1; j < RAY_LENGTH + 1; j++) {
            // We need to "snap" the position to a tile, which is why these conversions are done
            u16 xRay = FIXED_TO_INT(FIXED(FIXED_TO_INT(playerX))+j*xDir);
            u16 yRay = FIXED_TO_INT(FIXED(FIXED_TO_INT(playerY))+j*yDir);
            if (pixel_in_collision(xRay, yRay)) {
                s16 yDist = yRay - FIXED_TO_INT(FIXED(FIXED_TO_INT(playerY)));
                s16 xDist = xRay - FIXED_TO_INT(FIXED(FIXED_TO_INT(playerX)));
                dist = sqrt(yDist*yDist + xDist*xDist);
                break;
            }
        }
        // If wall within range
        s16 lineHeight = SCREEN_HEIGHT/dist;
        s16 offset = SCREEN_HEIGHT/2 - lineHeight/2;
        u16 num_rays = FOV/resolution;
        u16 sliceW = SCREEN_WIDTH/num_rays;
        u16 rayIndex = (i + FOV/2)/resolution;
        u16 leftX = rayIndex * sliceW;
        // Draw floor
        m4_rect(leftX, offset + lineHeight, leftX + sliceW, SCREEN_HEIGHT, FLOOR_COLOR_IDX);
        // Draw walls
        if (dist < RAY_LENGTH) {
            m4_rect(leftX, offset, leftX + sliceW, offset + lineHeight, color);
        }
        tte_write("#{P:50,145}");
        tte_erase_line();
        tte_printf("dist: %d", fps);
    }
}

static inline s16 clamp_steps(
    s16 currentAxisCoord,
    s16 delta,
    s16 otherAxisCoord,
    bool isVertical
    ) 
{
    if (delta == 0) return 0;
    s16  sign  = (delta > 0) ?  1 : -1;
    u16  steps = abs(delta);
    for (u16 i = 1; i <= steps; ++i) {
        s16 x = isVertical ? otherAxisCoord : currentAxisCoord + sign * i;
        s16 y = isVertical ? currentAxisCoord + sign * i : otherAxisCoord;
        if (pixel_in_collision(x, y))
            return sign * (i - 1);
    }
    return sign * steps;
}

void update_player() {
    u16 prevX = FIXED_TO_INT(playerX);
    u16 prevY = FIXED_TO_INT(playerY);
    u16 newY = prevY;

    key_poll();

    s16 moveX = 0, moveY = 0, rotateTheta = 0;

    s16 ticsPerSec = FIXED(dt) / SYSCLK_64;
    s16 linearMove = LINEAR_SPEED * ticsPerSec;
    s16 angularMove = ANGULAR_SPEED * ticsPerSec;
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
    s16 fixedDeltaY = FIXED_TO_INT(moveY * yDir) + FIXED_TO_INT(moveX * yLatDir);
    s16 deltaY = FIXED_TO_INT(fixedDeltaY);
    s16 safeStepsY = clamp_steps(prevY, deltaY, prevX, true);
    playerY += FIXED(safeStepsY);
    newY = FIXED_TO_INT(playerY);

    s16 xDir = lu_cos(playerTheta);
    s16 xLatDir = lu_cos(playerTheta - LU_PI/2);
    s16 fixedDeltaX = FIXED_TO_INT(moveY * xDir) + FIXED_TO_INT(moveX * xLatDir);
    s16 deltaX = FIXED_TO_INT(fixedDeltaX);
    s16 safeStepsX = clamp_steps(prevX, deltaX, newY, false);
    playerX += FIXED(safeStepsX);

    render_direction(WALL_COLOR_IDX);
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

