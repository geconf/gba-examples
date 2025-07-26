#include "tonc_input.h"
#include "tonc_math.h"
#include "tonc_memdef.h"
#include "tonc_tte.h"
#include <tonc.h>
#include <tonc_core.h>

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 160
#define VRAM ((volatile u16*)0x06000000)

// Fixed-point math
#define FIXED_SHIFT  12
// Convert to Fixed point
#define FIXED(x)     ((int)((x) * (1 << FIXED_SHIFT))) 
// Convert to integer. It adds half the divisor to round up
#define FIXED_TO_INT(x) ((x + (1 << (FIXED_SHIFT - 1))) >> FIXED_SHIFT) 
#define LU_PI 0x8000

// Speed
const int LINEAR_SPEED = FIXED(2);
const int ANGULAR_SPEED = LU_PI/15;

// FOV
const int FOV = LU_PI/2;

// Tile size
const int TILE_SIZE = 8;

// Simple 8×8 maze (1 = wall, 0 = empty space)
const int MAP_WIDTH = 8;
const int MAP_HEIGHT = 8;
const unsigned char worldMap[8][8] = {
    {1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 1, 0, 1, 1},
    {1, 0, 1, 0, 1, 0, 0, 1},
    {1, 0, 0, 0, 1, 1, 0, 1},
    {1, 0, 1, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}
};
const int MAP_X = 80;
const int MAP_Y = 40;

// Player data
const int PLAYER_START_X = FIXED(MAP_X+1*TILE_SIZE) + FIXED(TILE_SIZE/2);
const int PLAYER_START_Y = FIXED(MAP_Y+6*TILE_SIZE) + FIXED(TILE_SIZE/2);
int playerX = PLAYER_START_X;
int playerY = PLAYER_START_Y;
int playerPrevX = PLAYER_START_X;
int playerPrevY = PLAYER_START_Y;
const int PLAYER_START_THETA = FIXED(0);
int playerTheta = PLAYER_START_THETA;
u8 playerColor = 2;


static inline u8* back_page(void) {
    return (u8*)0x06000000
         + ((REG_DISPCNT & DCNT_PAGE) ? 0x0000 : 0xA000);
}


void draw_tile(int x, int y, u8 color) {
    for (int dy = 0; dy < TILE_SIZE; dy++) {
        for (int dx = 0; dx < TILE_SIZE; dx++) {
            m4_plot(x+dy, y+dx, color);
        }
    }
}

void draw_map(int x, int y) {
    for (int i = 0; i < MAP_HEIGHT; i++) {
        for (int j = 0; j < MAP_WIDTH; j++) {
            u8 color = 3;
            if (worldMap[i][j])
            {
                color = 5;
            }
            draw_tile(j*TILE_SIZE+x, i*TILE_SIZE+y, color);
        }
    }
}

void render_player(int x, int y, u8 color){
    m4_plot(x, y, color);
}


int player_in_collision(int x, int y){
    int playerXTile= (x-MAP_X)/TILE_SIZE;
    int playerYTile= (y-MAP_Y)/TILE_SIZE;
    return worldMap[playerYTile][playerXTile];
}


void render_direction(u8 color) {
    tte_write("#{P:50,0}");
    tte_erase_line();
    tte_printf("Player X: %d", FIXED_TO_INT(playerX));
    tte_write("#{P:50,10}");
    tte_erase_line();
    tte_printf("Player Y: %d", FIXED_TO_INT(playerY));
    tte_write("#{P:50,105}");
    tte_write("#{P:50,20}");
    tte_erase_line();
    tte_printf("Player X in fixed: %d", playerX);
    tte_write("#{P:50,30}");
    tte_erase_line();
    tte_printf("Player Y in fixed: %d", playerY);
    tte_write("#{P:50,105}");
    tte_erase_line();
    tte_printf("Player theta: %d", playerTheta);
    tte_write("#{P:50,115}");
    tte_erase_line();
    u32 x_dir = lu_cos(playerTheta);
    u32 y_dir = lu_sin(playerTheta);
    tte_printf("Cos Player theta: %d", x_dir);
    tte_write("#{P:50,125}");
    tte_erase_line();
    tte_printf("Sine Player theta: %d", y_dir);
    tte_write("#{P:50,135}");
    tte_erase_line();
    tte_printf("X dir to plot: %d", FIXED_TO_INT(playerX+x_dir));
    tte_write("#{P:50,145}");
    tte_erase_line();
    tte_printf("Y dir to plot: %d", FIXED_TO_INT(playerY+y_dir));
    int rayLength = 50;
    int fov = LU_PI/2;
    for (int i = -fov/2; i < fov/2+1; i = i + LU_PI/275) {
        for (int j = 1; j < rayLength + 1; j++) {
            u32 xDir = lu_cos(playerTheta + i);
            u32 yDir = lu_sin(playerTheta + i);
            // We need to "snap" the position to a certain tile, which is why these conversions are done
            u32 xRay = FIXED_TO_INT(FIXED(FIXED_TO_INT(playerX))+j*xDir);
            u32 yRay = FIXED_TO_INT(FIXED(FIXED_TO_INT(playerY))+j*yDir);
            if (player_in_collision(xRay, yRay))
            {
                break;
            }
            m4_plot(xRay, yRay, color);
        }
    }
}

void update_player(int dt_fp) {
    int prevX = FIXED_TO_INT(playerX);
    int prevY = FIXED_TO_INT(playerY);
    int newX = prevX, newY = prevY;

    key_poll();

    int moveX = 0, moveY = 0, rotateTheta = 0;

    if (key_is_down(KEY_UP))    moveY = -LINEAR_SPEED;
    if (key_is_down(KEY_DOWN))  moveY = LINEAR_SPEED;
    if (key_is_down(KEY_B))  moveX = -LINEAR_SPEED;
    if (key_is_down(KEY_A)) moveX = LINEAR_SPEED;
    if (key_is_down(KEY_LEFT)) rotateTheta = -ANGULAR_SPEED;
    if (key_is_down(KEY_RIGHT)) rotateTheta = ANGULAR_SPEED;

    // Apply Y movement first
    if (!player_in_collision(prevX, FIXED_TO_INT(playerY + moveY))) {
        playerY += moveY;
    }
    // Done in case moveY is too large
    else if (moveY > 0  && !player_in_collision(prevX, FIXED_TO_INT(playerY)+1)) {
        playerY += FIXED(1);
    }
    else if (moveY < 0  && !player_in_collision(prevX, FIXED_TO_INT(playerY)-1)) {
        playerY -= FIXED(1);
    }

    // Apply X movement after
    if (!player_in_collision(FIXED_TO_INT(playerX + moveX), newY)) {
        playerX += moveX;
    }
    else if (moveX > 0  && !player_in_collision(FIXED_TO_INT(playerX)+1, newY)) {
        playerX += FIXED(1);
    }
    else if (moveX < 0  && !player_in_collision(FIXED_TO_INT(playerX)-1, newY)) {
        playerX -= FIXED(1);
    }

    newX = FIXED_TO_INT(playerX);
    newY = FIXED_TO_INT(playerY);
    // Apply Rotation. No need to check for collisions in a raycaster
    playerTheta += rotateTheta;

    render_player(newX, newY, playerColor);
    render_direction(1);
}


static u16 lastTicks;
static int dt_fp;

void init_timebase(void) {
    REG_TM0CNT_L = 0;
    // start at SYSCLK (16.78 MHz)
    REG_TM0CNT_H = TM_ENABLE | TM_FREQ_64;
    lastTicks    = REG_TM0CNT_L;
}


void calc_delta_time(void) {
    u16 now  = REG_TM0CNT_L;
    u16 diff = now - lastTicks;   // wraps OK on 16 bits
    lastTicks = now;

    // 2^18 Hz timer → shift = 18 – 12 = 6
    dt_fp = diff >> 6;
    u16 fps = diff
             ? (262144 + diff/2) / diff   // integer divide with 0.5‑tick rounding
             : 0;
    tte_write("#{P:150,120}");
    tte_erase_line();
    tte_printf("dt_ms: %d", fps);
}


int main() {
    REG_DISPCNT = DCNT_MODE4 | DCNT_BG2;

    tte_init_bmp(DCNT_MODE4, NULL, NULL);
    tte_init_con();
    init_timebase();

    // Set up colors
    pal_bg_mem[0] = RGB15(0, 0, 0) | BIT(15);   // Black background
    pal_bg_mem[5] = RGB15(16, 0, 31) | BIT(15); // Purple walls
    pal_bg_mem[2] = RGB15(0, 31, 0) | BIT(15);  // Green player
    pal_bg_mem[3] = RGB15(16, 0, 0) | BIT(15);  // Red ground
    pal_bg_mem[1] = RGB15(0, 0, 31) | BIT(15);  // Blue direction

    while (1) {
        vid_vsync();
        calc_delta_time();

        TTC *tc = tte_get_context();
        tc->dst.data  = back_page();
        tc->dst.pitch = SCREEN_WIDTH;

        draw_map(MAP_X, MAP_Y);
        //update_player(dt_fp);
        vid_flip();
    }
}

