#include "tonc_input.h"
#include "tonc_math.h"
#include "tonc_memmap.h"
#include "tonc_video.h"
#include "inv_sin_lut.h"
#include "line_height_lut.h"


#define IWRAM_ARM IWRAM_CODE __attribute__((target("arm")))

// typedef
typedef s32 fx12;
typedef u16 lu_angle;

typedef enum {
    RAY_HIT_X,
    RAY_HIT_Y
} RayHitSide;

typedef struct {
    fx12 dist;
    RayHitSide side;
} RayHit;


// Fixed-point math
enum FixedShiftConsts {
    FIXED_SHIFT = 12,
    FIXED_HALF = 1 << (FIXED_SHIFT - 1),
};

// Convert to Fixed point for macros
#define INT_TO_FIXED(x) ((int)((x) << FIXED_SHIFT))

enum MathConsts {
    LU_PI = 0x8000,
    LU_HALF_PI = LU_PI >> 1,
    LU_3HALF_PI = LU_PI + LU_HALF_PI,
    FX_INF = 0x7FFFFFFF
};

enum TimeConsts {
    SYSCLK_64 = 262144,
    SYSCLK_64_SHIFT = 18
};

enum MapConsts {
    TILE_SIZE = 8,
    TILE_SIZE_FX = INT_TO_FIXED(8),
    TILE_SHIFT = 3,
    HALF_TILE_FX = TILE_SIZE_FX >> 1,
    MAP_WIDTH = 9,
    MAP_HEIGHT = 9,
    LINE_HEIGHT_LUT_SHIFT = TILE_SHIFT + FIXED_SHIFT - LINE_HEIGHT_LUT_FRAC_BITS,
};

enum ColorConsts {
    BLACK_COLOR_IDX = 0,
    DIR_COLOR_IDX = 1,
    PLAYER_COLOR_IDX = 2,
    FLOOR_COLOR_IDX = 3,
    LIGHT_WALL_COLOR_IDX = 4,
    DARK_WALL_COLOR_IDX = 5,
};

#define PIXEL4_WORD(c) \
    ((c) | ((c) << 8) | ((c) << 16) | ((c) << 24))

enum VideoFillConsts {
    SKY_FILL_WORD = PIXEL4_WORD(BLACK_COLOR_IDX),
    FLOOR_FILL_WORD = PIXEL4_WORD(FLOOR_COLOR_IDX)
};

enum PlayerConsts {
    PLAYER_RADIUS = TILE_SIZE_FX >> 2,
    PLAYER_RADIUS_SQUARED = (PLAYER_RADIUS * PLAYER_RADIUS) >> FIXED_SHIFT,
    FOV = LU_PI >> 1,
    HALF_FOV = FOV >> 1,
    RAY_LENGTH = INT_TO_FIXED(100),
    RAY_PIXEL_SCALE_SHIFT = 1, // 120 Rays
    RAY_COUNT = SCREEN_WIDTH >> RAY_PIXEL_SCALE_SHIFT,
    RAY_STEP = FOV / RAY_COUNT,
    LINEAR_SPEED = 13,
    ANGULAR_SPEED = LU_PI >> 13,
    PLAYER_START_X = INT_TO_FIXED(2*TILE_SIZE) + INT_TO_FIXED(TILE_SIZE >> 1),
    PLAYER_START_Y = INT_TO_FIXED(5*TILE_SIZE) + INT_TO_FIXED(TILE_SIZE >> 1),
    PLAYER_START_THETA = 0,
};

enum HardwareConsts {
    HALF_SCREEN_HEIGHT = SCREEN_HEIGHT >> 1,
    SCREEN_HEIGHT_FX = INT_TO_FIXED(SCREEN_HEIGHT),
    HALF_SCREEN_HEIGHT_FX = SCREEN_HEIGHT_FX >> 1,
};


enum VideoConsts {
    PIXELS_PER_U16 = 2,
    FRAMEBUFFER_STRIDE_U16 = SCREEN_WIDTH / PIXELS_PER_U16,
};

static const u8 worldMap[MAP_WIDTH * MAP_HEIGHT] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 1, 1, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 0, 0, 1, 1,
    1, 0, 0, 1, 0, 0, 0, 1, 1,
    1, 0, 0, 0, 0, 1, 0, 0, 1,
    1, 0, 0, 0, 0, 1, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1
};

// Walls
static s16 wallTop[SCREEN_WIDTH];
static s16 wallBottom[SCREEN_WIDTH];
static u8 wallAxis[SCREEN_WIDTH];

// Fish eye correction
static fx12 fishEyeCorrection[RAY_COUNT];

static inline void init_fish_eye_correction(void)
{
    lu_angle angle = -HALF_FOV;

    for (int i = 0; i < RAY_COUNT; i++) {
        fishEyeCorrection[i] = lu_cos(angle);
        angle += RAY_STEP;
    }
}

// Convert to Fixed point
static inline fx12 int_to_fixed(s32 x) {
    return x << FIXED_SHIFT;
}

// Convert to integer. It adds half the divisor to round up
static inline s32 fixed_to_int_s(fx12 x) {
    const s32 half = 1 << (FIXED_SHIFT - 1);
    if (x >= 0) {
        return (x + half) >> FIXED_SHIFT;
    }
    else {
        return (x - half) >> FIXED_SHIFT;
    }
}

static inline u32 fixed_to_int_u(u32 x) {
    const u32 half = 1u << (FIXED_SHIFT - 1);
    return (x + half) >> FIXED_SHIFT;
}

#define fixed_to_int(x) _Generic((x), \
s32: fixed_to_int_s,    \
u32: fixed_to_int_u,   \
default: fixed_to_int_s \
)(x)


// Player position
static fx12 playerX = PLAYER_START_X;
static fx12 playerY = PLAYER_START_Y;

// Player rotation
static lu_angle playerTheta = PLAYER_START_THETA;

// Time
static u32 lastTicks;
static u32 fps;
static u16 dt;

// debug
volatile u32 debug_work_fps;
volatile u32 debug_actual_fps;
volatile u16 debug_frame_ticks;
volatile u32 debug_update_ticks;
volatile u16 debug_raycast_ticks;
volatile u16 debug_render_ticks;
volatile u16 debug_work_ticks;

static inline u8* back_page(void) {
    return (u8*)0x06000000
         + ((REG_DISPCNT & DCNT_PAGE) ? 0x0000 : 0xA000);
}

static inline s32 fixed_mul(s32 a, s32 b) {
    return (s32)(((s64)a * b) >> FIXED_SHIFT);
}

static inline u32 fixed_abs(s32 x) {
    return x < 0 ? -x : x;
}


static inline u32 pixel_in_collision(u32 x, u32 y){
    u32 playerTileX = x >> TILE_SHIFT;
    u32 playerTileY = y >> TILE_SHIFT;
    return worldMap[playerTileY * MAP_WIDTH + playerTileX];
}

static inline POINT player_in_collision(
        s32 playerCenterX, s32 playerCenterY){
    s32 playerTileX = fixed_to_int(playerCenterX) >> TILE_SHIFT;
    s32 playerTileY = fixed_to_int(playerCenterY) >> TILE_SHIFT;
    POINT moveCoords = { 0, 0 };
    for (s32 i = -1; i < 2; i++) {
        for (s32 j = -1; j < 2; j++) {
            s32 neighborTileX = playerTileX + j;
            s32 neighborTileY = playerTileY + i;
            // Do not check out of bounds
            if (neighborTileX < 0 || neighborTileX >= MAP_WIDTH ||
                    neighborTileY < 0 || neighborTileY >= MAP_HEIGHT)
                continue;
            // Do not check for collisions if this is not a wall
            if (!worldMap[neighborTileY * MAP_WIDTH + neighborTileX]) {
                continue;
            }
            // Get the fixed coords for the AABB to check against
            s32 neighborX = int_to_fixed(neighborTileX * TILE_SIZE);
            s32 neighborY = int_to_fixed(neighborTileY * TILE_SIZE);
            s32 neighborCenterX = neighborX + HALF_TILE_FX;
            s32 neighborCenterY = neighborY + HALF_TILE_FX;
            s32 differenceX = playerCenterX - neighborCenterX;
            s32 differenceY = playerCenterY - neighborCenterY;
            s32 clampX = clamp(differenceX, -HALF_TILE_FX, HALF_TILE_FX);
            s32 clampY = clamp(differenceY, -HALF_TILE_FX, HALF_TILE_FX);
            s32 closestX = neighborCenterX + clampX;
            s32 closestY = neighborCenterY + clampY;
            s32 distanceX = closestX - playerCenterX;
            s32 distanceY = closestY - playerCenterY;
            s32 distanceSquared = fixed_mul(distanceX, distanceX) + fixed_mul(distanceY, distanceY);
            if (distanceSquared < PLAYER_RADIUS_SQUARED) {
                s32 penetrationX = PLAYER_RADIUS - fixed_abs(distanceX);
                s32 penetrationY = PLAYER_RADIUS - fixed_abs(distanceY);
                s32 moveX = penetrationX;
                s32 moveY = penetrationY;
                if (distanceX > 0) {
                    moveX = -moveX;
                }
                if (distanceY > 0) {
                    moveY = -moveY;
                }
                moveCoords.x += moveX;
                moveCoords.y += moveY;
            }
        }
    }
    return moveCoords;
}

IWRAM_ARM fx12 clamp_steps(
    fx12 currentAxisCoord,
    fx12 delta,
    fx12 otherAxisCoord,
    bool isVertical
    ) 
{
    if (delta == 0) return 0;
    s32  sign  = (delta > 0) ?  1 : -1;
    u32  steps = fixed_abs(delta);
    for (u16 i = 1; i <= steps; i = i + TILE_SIZE) {
        u32 x = isVertical ? otherAxisCoord : currentAxisCoord + sign * i;
        u32 y = isVertical ? currentAxisCoord + sign * i : otherAxisCoord;
        POINT moveCoords = player_in_collision(x, y);
        if (moveCoords.x != 0 || moveCoords.y != 0)
            return sign * (i - TILE_SIZE);
    }
    return sign * steps;
}

static inline void update_player() {
    key_poll();

    s16 moveX = 0, moveY = 0, rotateTheta = 0;

    // NOTE: Frame drops cause collision
    // detection to scale as clamp_steps() has to do more work as it
    // performs checks over a longer distance.
    // On GBA, frame rates are quantized (60/30/20/etc), so we
    // can exploit this
    // Note that for 60 fps, dt is 4389 (68 secs per frame)
    // for 30 fps, dt is 8778 (138 secs per frame). This can be baked
    // into the constants rather than calculated on every frame.
    s16 secPerFrame = int_to_fixed(dt) / SYSCLK_64;
    s16 linearMove = LINEAR_SPEED * secPerFrame;
    s16 angularMove = ANGULAR_SPEED * secPerFrame;

    if (key_is_down(KEY_UP)) moveY += linearMove;
    if (key_is_down(KEY_DOWN)) moveY += -linearMove;
    if (key_is_down(KEY_R)) moveX += -linearMove;
    if (key_is_down(KEY_L)) moveX += linearMove;
    if (key_is_down(KEY_LEFT)) rotateTheta += -angularMove;
    if (key_is_down(KEY_RIGHT)) rotateTheta += angularMove;
    // Handle moving diagonally at the same speed
    if (moveX && moveY)
    {
        // Multiply by 0.707
        moveX = (moveX * 181) >> 8;
        moveY = (moveY * 181) >> 8;
    }

    // Apply Rotation. No need to check for collisions in a raycaster
    playerTheta += rotateTheta;

    // Apply translation per axis
    fx12 yDir = lu_sin(playerTheta);
    fx12 yLatDir = lu_sin(playerTheta - (LU_PI >> 1));
    fx12 deltaY = fixed_mul(moveY, yDir) + fixed_mul(moveX, yLatDir);
    fx12 safeStepsY = clamp_steps(playerY, deltaY, playerX, true);
    playerY += safeStepsY;
    //playerY += deltaY;

    fx12 xDir = lu_cos(playerTheta);
    fx12 xLatDir = lu_cos(playerTheta - (LU_PI >> 1));
    fx12 deltaX = fixed_mul(moveY, xDir) + fixed_mul(moveX, xLatDir);
    fx12 safeStepsX = clamp_steps(playerX, deltaX, playerY, false);
    playerX += safeStepsX;
    //playerX += deltaX;
}

static inline void cast_rays() {
    lu_angle rayAngle = playerTheta - HALF_FOV;
    for (int i = 0; i < RAY_COUNT; i++ ) {
        fx12 xDir = lu_cos(rayAngle);
        fx12 yDir = lu_sin(rayAngle);
        fx12 dist = RAY_LENGTH;
        // First pass, coarser ray marching
        for (int j = 1; j < fixed_to_int(RAY_LENGTH) + 1; j = j + 1) {
            fx12 z = int_to_fixed(j);
            fx12 xRay = playerX+fixed_mul(z, xDir);
            fx12 yRay = playerY+fixed_mul(z, yDir);
            if (pixel_in_collision(fixed_to_int(xRay), fixed_to_int(yRay))) {
                dist = int_to_fixed(j - 1);
                break;
            }
        }
        fx12 xDist = fixed_mul(dist, xDir);
        fx12 yDist = fixed_mul(dist, yDir);
        // Second pass, finer
        for (fx12 j = 1; j < RAY_LENGTH + 1; j = j + 300) {
            fx12 xRay = playerX + xDist + fixed_mul(j, xDir);
            fx12 yRay = playerY + yDist + fixed_mul(j, yDir);
            if (pixel_in_collision(fixed_to_int(xRay), fixed_to_int(yRay))) {
                dist += j;
                break;
            }
        }
        // Fish-eye correction
        dist = fixed_mul(dist, fishEyeCorrection[i]);
        // If wall within range
        u32 index = dist >> (TILE_SHIFT + FIXED_SHIFT - 5);
        fx12 lineHeight = line_height_lut[index];
        if (lineHeight > int_to_fixed(SCREEN_HEIGHT)) {
            lineHeight = int_to_fixed(SCREEN_HEIGHT);
        }
        fx12 offset = (int_to_fixed(SCREEN_HEIGHT) >> 1) - (lineHeight >> 1);
        if (offset < 0) {
            offset = 0;
        }
        // Record wall
        if (dist < RAY_LENGTH) {
            wallTop[i] = fixed_to_int(offset);
            wallBottom[i] = fixed_to_int(offset + lineHeight);
        }
        rayAngle += RAY_STEP;
    }
}


static inline s32 angle_sign_cos(lu_angle a)
{
    return ((a + LU_HALF_PI) & LU_PI) ? -1 : 1;
}

static inline s32 angle_sign_sin(lu_angle a)
{
    return (a & LU_PI) ? -1 : 1;
}

typedef struct {
    fx12 boundaryDistX[2];
    fx12 boundaryDistY[2];
    u32 mapIndex;
} RayOrigin;

static inline RayHit cast_ray_dda(lu_angle rayAngle, RayOrigin rayOrigin) {
    const s32 signX = angle_sign_cos(rayAngle);
    const s32 signY = angle_sign_sin(rayAngle);
    const fx12 invXDir = lu_inv_abs_cos(rayAngle);
    const fx12 invYDir = lu_inv_abs_sin(rayAngle);

    u32 mapIndex = rayOrigin.mapIndex;
    fx12 boundaryDistX = signX > 0 ? 
        rayOrigin.boundaryDistX[0] : rayOrigin.boundaryDistX[1];
    fx12 boundaryDistY = signY > 0 ? 
        rayOrigin.boundaryDistY[0] : rayOrigin.boundaryDistY[1];

    fx12 tMaxX;
    fx12 tMaxY;
    fx12 tDeltaX;
    fx12 tDeltaY;

    RayHitSide side = RAY_HIT_X;

    fx12 dist = 0;
    if (invXDir == FX_INF) {
        tMaxX   = FX_INF;
        tDeltaX = FX_INF;
    }
    else {
        tMaxX   = fixed_mul(boundaryDistX, invXDir);
        tDeltaX = invXDir << TILE_SHIFT;
    }

    if (invYDir == FX_INF) {
        tMaxY   = FX_INF;
        tDeltaY = FX_INF;
    }
    else {
        tMaxY   = fixed_mul(boundaryDistY, invYDir);
        tDeltaY = invYDir << TILE_SHIFT;
    }

    while (1) {
        if (tMaxX < tMaxY) {
            mapIndex += signX;
            dist = tMaxX;
            tMaxX += tDeltaX;

            // entered tile through an X boundary
            side = RAY_HIT_X;
        }
        else {
            mapIndex += signY * MAP_WIDTH;
            dist = tMaxY;
            tMaxY += tDeltaY;

            // entered tile through a Y boundary
            side = RAY_HIT_Y;
        }

        if (worldMap[mapIndex]) {
            break;
        }
    }
    return (RayHit) {
        .dist = dist,
        .side = side
    };
}

IWRAM_ARM void cast_rays_dda(void) {
    lu_angle rayAngle = playerTheta - HALF_FOV;
    u32 playerTileX = fixed_to_int(playerX) >> TILE_SHIFT;
    u32 playerTileY = fixed_to_int(playerY) >> TILE_SHIFT;

    RayOrigin rayOrigin;
    rayOrigin.mapIndex = playerTileY * MAP_WIDTH + playerTileX;

    fx12 left   = int_to_fixed(playerTileX << TILE_SHIFT);
    fx12 right  = left + TILE_SIZE_FX;
    fx12 top    = int_to_fixed(playerTileY << TILE_SHIFT);
    fx12 bottom = top + TILE_SIZE_FX;

    rayOrigin.boundaryDistX[0] = right - playerX;
    rayOrigin.boundaryDistX[1] = playerX - left;

    rayOrigin.boundaryDistY[0] = bottom - playerY;
    rayOrigin.boundaryDistY[1] = playerY - top;

    for (int i = 0; i < RAY_COUNT; i++ ) {
        RayHit rayHit = cast_ray_dda(rayAngle, rayOrigin);
        fx12 dist = rayHit.dist;
        wallAxis[i] = rayHit.side;

        // Fish-eye correction
        dist = fixed_mul(dist, fishEyeCorrection[i]);
        // If wall within range
        u32 index = dist >> LINE_HEIGHT_LUT_SHIFT;
        fx12 lineHeight = line_height_lut[index];
        if (lineHeight > SCREEN_HEIGHT_FX) {
            lineHeight = SCREEN_HEIGHT_FX;
        }
        fx12 offset = HALF_SCREEN_HEIGHT_FX - (lineHeight >> 1);
        // Record wall
        wallTop[i] = fixed_to_int(offset);
        wallBottom[i] = fixed_to_int(offset + lineHeight);
        rayAngle += RAY_STEP;
    }
}

IWRAM_ARM void render_frame() {
    u8 *page = back_page();

    dma3_fill(
        page,
        SKY_FILL_WORD,
        SCREEN_WIDTH * HALF_SCREEN_HEIGHT
    );

    dma3_fill(
        page + SCREEN_WIDTH * HALF_SCREEN_HEIGHT,
        FLOOR_FILL_WORD,
        SCREEN_WIDTH * HALF_SCREEN_HEIGHT
    );

    u16 *page16 = (u16 *)page;

    for (int x = 0; x < SCREEN_WIDTH; x += 2)
    {
        int ray0 = x >> RAY_PIXEL_SCALE_SHIFT;
        int ray1 = (x + 1) >> RAY_PIXEL_SCALE_SHIFT;

        int t0 = wallTop[ray0];
        int b0 = wallBottom[ray0];
        int t1 = wallTop[ray1];
        int b1 = wallBottom[ray1];

        u8 c0 = wallAxis[ray0]
            ? DARK_WALL_COLOR_IDX
            : LIGHT_WALL_COLOR_IDX;

        u8 c1 = wallAxis[ray1]
            ? DARK_WALL_COLOR_IDX
            : LIGHT_WALL_COLOR_IDX;

        u16 wall2 = c0 | (c1 << 8);

        // Both columns contain wall here.
        int top = t0 > t1 ? t0 : t1;
        int bot = b0 < b1 ? b0 : b1;

        u16 *dst = page16 + top * FRAMEBUFFER_STRIDE_U16 + (x >> 1);

        for (int y = top; y < bot; y++)
        {
            *dst = wall2;
            dst += FRAMEBUFFER_STRIDE_U16;
        }

        // Column x extends beyond x+1.
        if (t0 < t1)
        {
            dst = page16 + t0 * FRAMEBUFFER_STRIDE_U16 + (x >> 1);

            for (int y = t0; y < t1; y++)
            {
                *dst = (*dst & 0xFF00) | c0;
                dst += FRAMEBUFFER_STRIDE_U16;
            }

            dst = page16 + b1 * FRAMEBUFFER_STRIDE_U16 + (x >> 1);

            for (int y = b1; y < b0; y++)
            {
                *dst = (*dst & 0xFF00) | c0;
                dst += FRAMEBUFFER_STRIDE_U16;
            }
        }
        // Column x+1 extends beyond x.
        else if (t1 < t0)
        {
            dst = page16 + t1 * FRAMEBUFFER_STRIDE_U16 + (x >> 1);

            for (int y = t1; y < t0; y++)
            {
                *dst = (*dst & 0x00FF)
                     | (c1 << 8);
                dst += FRAMEBUFFER_STRIDE_U16;
            }

            dst = page16 + b0 * FRAMEBUFFER_STRIDE_U16 + (x >> 1);

            for (int y = b0; y < b1; y++)
            {
                *dst = (*dst & 0x00FF)
                     | (c1 << 8);
                dst += FRAMEBUFFER_STRIDE_U16;
            }
        }
    }
}

static inline void init_timebase(void) {
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


static inline u16 elapsed_ticks(u16 timer_start) {
    return REG_TM0CNT_L - timer_start;
}

static inline  u32 calculate_fps(u16 dt) {
    /* FPS = frames/seconds = 1/(diff * 1/262144)
     * Simplifying: FPS = 262144/diff.
     * Added diff/2 to round correctly */
    return dt
        ? (SYSCLK_64 + (dt >> 1)) / dt
        : 0;
}


int main() {
    REG_DISPCNT = DCNT_MODE4 | DCNT_BG2;

    init_timebase();

    init_fish_eye_correction();

    // Set up colors
    // Black background
    pal_bg_mem[BLACK_COLOR_IDX] = RGB15(0, 0, 0) | BIT(15);
    // Purple walls
    pal_bg_mem[LIGHT_WALL_COLOR_IDX] = RGB15(16, 0, 31) | BIT(15);
    pal_bg_mem[DARK_WALL_COLOR_IDX] = RGB15(8, 0, 16) | BIT(15);
    // Green player
    pal_bg_mem[PLAYER_COLOR_IDX] = RGB15(0, 31, 0) | BIT(15);
    // Red ground
    pal_bg_mem[FLOOR_COLOR_IDX] = RGB15(16, 0, 0) | BIT(15);
    // Blue direction
    pal_bg_mem[DIR_COLOR_IDX] = RGB15(0, 0, 31) | BIT(15);

    while (1) {
        u16 frame_start = REG_TM0CNT_L;
        u16 work_start = REG_TM0CNT_L;

        u16 update_start = REG_TM0CNT_L;
        update_player();
        debug_update_ticks = elapsed_ticks(update_start);

        u16 ray_start = REG_TM0CNT_L;
        cast_rays_dda();
        debug_raycast_ticks = elapsed_ticks(ray_start);

        u16 render_start = REG_TM0CNT_L;
        render_frame();
        debug_render_ticks = elapsed_ticks(render_start);

        u16 work_end = REG_TM0CNT_L;
        u16 frame_end = REG_TM0CNT_L;
        debug_work_ticks = work_end - work_start;
        debug_frame_ticks  = frame_end - frame_start;

        debug_work_fps = calculate_fps(debug_frame_ticks);

        vid_vsync();
        vid_flip();

        u16 now  = REG_TM0CNT_L;
        dt = now - lastTicks;
        lastTicks = now;

        fps = calculate_fps(dt);
        debug_actual_fps = fps;
    }
}

