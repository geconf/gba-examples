#include <tonc.h>

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 160
#define VRAM ((volatile u16*)0x06000000)

// Fixed-point math
#define FIXED_SHIFT  8
#define FIXED(x)     ((x) << FIXED_SHIFT)
#define FIXED_TO_INT(x) ((x) >> FIXED_SHIFT)

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

// Player position
int playerX = FIXED(3);
int playerY = FIXED(3);
int playerDirX = FIXED(1);  // Facing right
int playerDirY = FIXED(0);

// Function to plot a pixel in Mode 4
void plot_pixel(int x, int y, u8 color) {
    u16 *videoBuffer = (u16*)VRAM;
    int index = (y * SCREEN_WIDTH + x) / 2;

    if (x & 1) {
        videoBuffer[index] = (videoBuffer[index] & 0x00FF) | (color << 8);
    } else {
        videoBuffer[index] = (videoBuffer[index] & 0xFF00) | color;
    }
}

// Raycasting a single column at screenX
void raycast_column(int screenX) {
    int rayDirX = playerDirX;
    int rayDirY = playerDirY;

    int mapX = FIXED_TO_INT(playerX);
    int mapY = FIXED_TO_INT(playerY);

    int stepX, stepY;
    int sideDistX, sideDistY;
    int deltaDistX = FIXED(1);
    int deltaDistY = FIXED(1);

    if (rayDirX < 0) { stepX = -1; sideDistX = (playerX - FIXED(mapX)) * deltaDistX; }
    else { stepX = 1; sideDistX = (FIXED(mapX + 1) - playerX) * deltaDistX; }
    if (rayDirY < 0) { stepY = -1; sideDistY = (playerY - FIXED(mapY)) * deltaDistY; }
    else { stepY = 1; sideDistY = (FIXED(mapY + 1) - playerY) * deltaDistY; }

    int hit = 0, side;
    int steps = 0; // DEBUG: Count how many steps the ray takes

    while (!hit && steps < 100) { // Prevent infinite loops
        if (sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; }
        else { sideDistY += deltaDistY; mapY += stepY; side = 1; }
        
        steps++; // DEBUG: Increment step count

        if (worldMap[mapY][mapX] > 0) hit = 1;
    }

    // If no wall was hit, force one at a fixed distance
    if (!hit) {
        mapX = 4;
        mapY = 4;
        side = 0;
    }

    int perpWallDist = (side == 0) ? (mapX - playerX + (1 - stepX) / 2) * FIXED(1)
                                   : (mapY - playerY + (1 - stepY) / 2) * FIXED(1);
    if (side == 0 && rayDirX != 0) perpWallDist /= rayDirX;
    if (side == 1 && rayDirY != 0) perpWallDist /= rayDirY;

    int wallHeight = SCREEN_HEIGHT * FIXED(1) / perpWallDist;

    int drawStart = SCREEN_HEIGHT / 2 - FIXED_TO_INT(wallHeight) / 2;
    int drawEnd = SCREEN_HEIGHT / 2 + FIXED_TO_INT(wallHeight) / 2;
    drawStart = (drawStart < 0) ? 0 : drawStart;
    drawEnd = (drawEnd >= SCREEN_HEIGHT) ? SCREEN_HEIGHT - 1 : drawEnd;

    // DEBUG: Show steps taken by the ray with colors
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        if (y < drawStart) {
            plot_pixel(screenX, y, 2); // Sky: Green
        } else if (y >= drawStart && y < drawEnd) {
            plot_pixel(screenX, y, 5); // Wall: White
        } else {
            plot_pixel(screenX, y, 3); // Ground: Blue
        }
    }
}

int main() {
    REG_DISPCNT = DCNT_MODE4 | DCNT_BG2;

    // Set up colors
    pal_bg_mem[0] = RGB15(0, 0, 0) | BIT(15);   // Black background
    pal_bg_mem[5] = RGB15(31, 31, 31) | BIT(15); // White wall
    pal_bg_mem[2] = RGB15(0, 31, 0) | BIT(15);  // Green sky
    pal_bg_mem[3] = RGB15(0, 0, 31) | BIT(15);  // Blue ground

    while (1) {
        vid_vsync();
        dma3_cpy((void*)VRAM, (void*)0, (SCREEN_WIDTH * SCREEN_HEIGHT) / 2);
        raycast_column(120);
    }
}

