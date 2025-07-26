#include "tonc_video.h"
#include <tonc.h>
#include <stdlib.h>  // Required for rand()
#include <time.h>    // Required for seeding rand()

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 160
#define TILE_SIZE      8
#define MAX_LENGTH    100
#define MOVE_DELAY    5  // Number of frames before moving

// Directions
#define UP    0
#define DOWN  1
#define LEFT  2
#define RIGHT 3

typedef struct {
    int x, y;
} Point;

Point snake[MAX_LENGTH];
int snake_length = 5;
int direction = RIGHT;
Point food;
int frame_counter = 0;  // For delaying movement

void spawn_food() {
    do {
        food.x = (rand() % (SCREEN_WIDTH / TILE_SIZE)) * TILE_SIZE;
        food.y = (rand() % (SCREEN_HEIGHT / TILE_SIZE)) * TILE_SIZE;

        // Ensure food is not placed on the snake
        int valid = 1;
        for (int i = 0; i < snake_length; i++) {
            if (snake[i].x == food.x && snake[i].y == food.y) {
                valid = 0;
                break;
            }
        }
        if (valid) break;  // Exit loop if food is placed correctly

    } while (1);
}

void update_snake() {
    if (frame_counter++ < MOVE_DELAY) return; // Slow down movement
    frame_counter = 0; // Reset counter

    for (int i = snake_length - 1; i > 0; i--) {
        snake[i] = snake[i - 1];
    }

    // Move head
    switch (direction) {
        case UP:    snake[0].y -= TILE_SIZE; break;
        case DOWN:  snake[0].y += TILE_SIZE; break;
        case LEFT:  snake[0].x -= TILE_SIZE; break;
        case RIGHT: snake[0].x += TILE_SIZE; break;
    }

    // Wrap around screen
    if (snake[0].x < 0) snake[0].x = SCREEN_WIDTH - TILE_SIZE;
    if (snake[0].x >= SCREEN_WIDTH) snake[0].x = 0;
    if (snake[0].y < 0) snake[0].y = SCREEN_HEIGHT - TILE_SIZE;
    if (snake[0].y >= SCREEN_HEIGHT) snake[0].y = 0;

    // Check collision with food
    if (snake[0].x == food.x && snake[0].y == food.y) {
        if (snake_length < MAX_LENGTH) snake_length++;
        spawn_food();
    }

    // Check self-collision
    for (int i = 1; i < snake_length; i++) {
        if (snake[0].x == snake[i].x && snake[0].y == snake[i].y) {
            snake_length = 5; // Reset snake
            spawn_food();
            break;
        }
    }
}

void draw_snake() {
    for (int i = 0; i < snake_length; i++) {
        m3_plot(snake[i].x, snake[i].y, RGB15(31, 31, 31));
    }
}

void draw_food() {
    m3_plot(food.x, food.y, RGB15(31, 0, 0));
}

void handle_input() {
    if (key_hit(KEY_UP) && direction != DOWN) direction = UP;
    if (key_hit(KEY_DOWN) && direction != UP) direction = DOWN;
    if (key_hit(KEY_LEFT) && direction != RIGHT) direction = LEFT;
    if (key_hit(KEY_RIGHT) && direction != LEFT) direction = RIGHT;
}

int main() {
    REG_DISPCNT = DCNT_MODE3 | DCNT_BG2;
    
    srand(time(NULL));  // Properly seed random number generator
    spawn_food();

    while (1) {
        vid_vsync();
        key_poll();
        m3_fill(RGB15(0, 0, 0)); // Clear screen
        handle_input();
        update_snake();
        draw_snake();
        draw_food();
    }
}

