local devkitarm = assert(os.getenv("DEVKITARM"), "DEVKITARM is not set")
local nm = devkitarm .. "/bin/arm-none-eabi-nm"

 local elf = "m4-raycaster.elf"
local tmp = ".mgba-symbols.txt"

os.execute(string.format(
    '"%s" -n "%s" | grep " debug_" > "%s"',
    nm,
    elf,
    tmp
))

local symbols = {}

local f = assert(io.open(tmp, "r"), "could not open nm output")

for line in f:lines() do
    local addr, _, name = line:match("^(%x+)%s+(%S+)%s+(%S+)$")

    if addr and name then
        symbols[name] = tonumber(addr, 16)
    end
end

f:close()

local WORK_ADDR = assert(
    symbols.debug_work_ticks,
    "debug_work_ticks not found"
)

local WORK_FPS_ADDR = assert(
    symbols.debug_work_fps,
    "debug_work_fps not found"
)

local ACTUAL_FPS_ADDR = assert(
    symbols.debug_actual_fps,
    "debug_actual_fps not found"
)

local UPDATE_ADDR = assert(
    symbols.debug_update_ticks,
    "debug_update_ticks not found"
)

local RAYCAST_ADDR = assert(
    symbols.debug_raycast_ticks,
    "debug_raycast_ticks not found"
)

local RENDER_ADDR = assert(
    symbols.debug_render_ticks,
    "debug_render_ticks not found"
)

console:log(string.format(
    "Profiler symbols: work=%08X update=%08X, raycast=%08X, render=%08X, work_fps=%08X, actual_fps=%08X",
    WORK_ADDR,
    UPDATE_ADDR,
    RAYCAST_ADDR,
    RENDER_ADDR,
    WORK_FPS_ADDR,
    ACTUAL_FPS_ADDR
))

local TIMER_HZ = 262144

callbacks:add("frame", function()
    local work = emu:read16(WORK_ADDR)
    local work_fps = emu:read32(WORK_FPS_ADDR)
    local actual_fps = emu:read32(ACTUAL_FPS_ADDR)
    local update = emu:read16(UPDATE_ADDR)
    local raycast = emu:read16(RAYCAST_ADDR)
    local render = emu:read16(RENDER_ADDR)

    console:log(string.format(
        "work: %.2f ms | update: %.2f ms | raycast: %.2f ms | render: %.2f ms | work fps: %d | actual fps: %d",
        work  * 1000 / TIMER_HZ,
        update * 1000 / TIMER_HZ,
        raycast * 1000 / TIMER_HZ,
        render * 1000 / TIMER_HZ,
        work_fps,
        actual_fps
    ))
end)
