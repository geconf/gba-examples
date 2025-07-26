#include "tonc_input.h"
#include "tonc_memdef.h"
#include <tonc.h>

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 160
#define VRAM ((volatile u16*)0x06000000)

// Fixed-point math
#define FIXED_SHIFT  8
#define FIXED(x)     ((int)((x) * (1 << FIXED_SHIFT)))  // Convert to fixed-point (multiply by 256)
#define FIXED_TO_INT(x) ((x) >> FIXED_SHIFT)  // Extract integer part (divide by 256)


// Speed
const int SPEED = FIXED(0.6);

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
const int PLAYER_SIZE = 4;

// Player position
const int PLAYER_START_X = FIXED(MAP_X+1*TILE_SIZE) + FIXED(TILE_SIZE/2);
const int PLAYER_START_Y = FIXED(MAP_Y+6*TILE_SIZE) + FIXED(TILE_SIZE/2);
int playerX = PLAYER_START_X;
int playerY = PLAYER_START_Y;
int playerPrevX = PLAYER_START_X;
int playerPrevY = PLAYER_START_Y;
u8 playerColor = 2;

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
    for (int i = 0; i < PLAYER_SIZE; i++) {
        for (int j = 0; j < PLAYER_SIZE; j++) {
            m4_plot(x+i, y+j, color);
        }
    }
}

int player_in_collision(int x, int y){
    int playerXTile= (x-MAP_X)/TILE_SIZE;
    int playerXTileRight = (x-MAP_X+(PLAYER_SIZE-1))/TILE_SIZE;
    int playerYTile= (y-MAP_Y)/TILE_SIZE;
    int playerYTileBottom = (y-MAP_Y+(PLAYER_SIZE-1))/TILE_SIZE;
    return worldMap[playerYTile][playerXTile] ||
        worldMap[playerYTile][playerXTileRight] ||
        worldMap[playerYTileBottom][playerXTile] ||
        worldMap[playerYTileBottom][playerXTileRight];
}


void update_player() {
    int prevX = FIXED_TO_INT(playerX);
    int prevY = FIXED_TO_INT(playerY);
    int newX = prevX, newY = prevY;

    key_poll();

    int moveX = 0, moveY = 0;

    if (key_is_down(KEY_UP))    moveY = -SPEED;
    if (key_is_down(KEY_DOWN))  moveY = SPEED;
    if (key_is_down(KEY_LEFT))  moveX = -SPEED;
    if (key_is_down(KEY_RIGHT)) moveX = SPEED;

    // Apply Y movement first
    if (!player_in_collision(prevX, FIXED_TO_INT(playerY + moveY))) {
        playerY += moveY;
        newY = FIXED_TO_INT(playerY);
    }

    // Apply X movement after
    if (!player_in_collision(FIXED_TO_INT(playerX + moveX), newY)) {
        playerX += moveX;
        newX = FIXED_TO_INT(playerX);
    }

    // Erase old position only if moved
    if (newX != prevX || newY != prevY) {
        render_player(prevX, prevY, 3);
    }

    render_player(newX, newY, playerColor);
}



int main() {
    REG_DISPCNT = DCNT_MODE4 | DCNT_BG2;

    // Set up colors
    pal_bg_mem[0] = RGB15(0, 0, 0) | BIT(15);   // Black background
    pal_bg_mem[5] = RGB15(16, 0, 31) | BIT(15); // Purple walls
    pal_bg_mem[2] = RGB15(0, 31, 0) | BIT(15);  // Green player
    pal_bg_mem[3] = RGB15(16, 0, 0) | BIT(15);  // Red ground
    pal_bg_mem[1] = RGB15(0, 0, 31) | BIT(15);  // Blue alt

    while (1) {
        vid_vsync();
        draw_map(MAP_X, MAP_Y);
        update_player();
        vid_flip();
    }
}

