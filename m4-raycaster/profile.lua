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

local UPDATE_ADDR = assert(
    symbols.debug_update_ticks,
    "debug_update_ticks not found"
)

local FRAME_ADDR = assert(
    symbols.debug_frame_ticks,
    "debug_frame_ticks not found"
)

local FPS_ADDR = assert(
    symbols.debug_fps,
    "debug_fps not found"
)

local RAYCAST_ADDR = assert(
    symbols.debug_raycast_ticks,
    "debug_raycast_ticks not found"
)

console:log(string.format(
    "Profiler symbols: update=%08X raycast=%08X frame=%08X fps=%08X",
    UPDATE_ADDR,
    RAYCAST_ADDR,
    FRAME_ADDR,
    FPS_ADDR
))

local TIMER_HZ = 262144

callbacks:add("frame", function()
    local update  = emu:read16(UPDATE_ADDR)
    local frame   = emu:read16(FRAME_ADDR)
    local fps     = emu:read32(FPS_ADDR)
    local raycast = emu:read16(RAYCAST_ADDR)

    console:log(string.format(
        "update: %.2f ms | raycast: %.2f ms | frame: %.2f ms | fps: %d",
        update  * 1000 / TIMER_HZ,
        raycast * 1000 / TIMER_HZ,
        frame   * 1000 / TIMER_HZ,
        fps
    ))
end)
