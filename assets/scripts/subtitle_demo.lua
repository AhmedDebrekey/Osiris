-- Demonstrates the ui.Text global: fades a line of subtitle text in, holds it, then fades it
-- back out, using MoveTowards to animate opacity frame-rate-independently.

local alpha = 0.0
local elapsed = 0.0

local HOLD_END     = 3.0
local FADE_OUT_END = 4.0

function OnStart()
    print("subtitle_demo: started")
end

function OnUpdate(dt)
    elapsed = elapsed + dt

    local targetAlpha = 0.0
    if elapsed < HOLD_END then
        targetAlpha = 1.0
    elseif elapsed < FADE_OUT_END then
        targetAlpha = 0.0
    end

    alpha = MoveTowards(alpha, targetAlpha, dt * 1.5)

    if alpha > 0.0 then
        ui.Text({
            x = 0.5, y = 0.88, anchor = "center",
            text = "Look at you.",
            color = {1, 1, 1, alpha},
            size = 24,
        })
    end
end

function OnFixedUpdate(fixedDt)
end
