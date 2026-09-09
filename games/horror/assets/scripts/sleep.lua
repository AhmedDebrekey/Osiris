local fadeAlpha = 0.0
local fadeState = 0
local blackTimer = 0.0

-- Fade speed in alpha per second.
-- 1.0 = roughly 1 second to fade in/out.
local fadeSpeed = 1.0

-- How long the screen stays completely black.
local sleepDuration = 1.0

local function story()
    _G.ApartmentStory = _G.ApartmentStory or {}
    _G.ApartmentStory.checked = _G.ApartmentStory.checked or {}
    return _G.ApartmentStory
end

function OnStart()
    fadeAlpha = 0.0
    fadeState = 0
    blackTimer = 0.0
end

function OnInteract(interactor)
    -- Only start sleeping if we're currently idle.
    if fadeState == 0 then
        fadeState = 1
        blackTimer = 0.0
        local state = story()
        state.checked.Bed = true
        state.sleeping = true
    end
end

function OnUpdate(dt)
    -- 0 = idle
    if fadeState == 0 then
        return
    end

    -- 1 = fade to black
    if fadeState == 1 then
        fadeAlpha = MoveTowards(
            fadeAlpha,
            1.0,
            dt * fadeSpeed
        )

        if fadeAlpha >= 1.0 then
            fadeAlpha = 1.0
            blackTimer = 0.0
            fadeState = 2
        end

    -- 2 = stay black for a moment
    elseif fadeState == 2 then
        blackTimer = blackTimer + dt

        if blackTimer >= sleepDuration then
            fadeState = 3
        end

    -- 3 = fade back to normal
    elseif fadeState == 3 then
        fadeAlpha = MoveTowards(
            fadeAlpha,
            0.0,
            dt * fadeSpeed
        )

        if fadeAlpha <= 0.0 then
            fadeAlpha = 0.0
            fadeState = 0
            story().sleeping = false
        end
    end

    ui.FadeToBlack(fadeAlpha)
end
