local light = nil

-- The normal intensity is read from the SpotLight when the game starts.
local normalIntensity = 0.0

-- Time until the next flicker burst.
local waitTimer = 0.0
local nextFlicker = 2.0

-- Flicker sequence state.
local flickering = false
local flickerStep = 1
local flickerTimer = 0.0

-- Total running time, used to vary the delay between flickers.
local time = 0.0


-- Each entry is:
-- { duration, brightness multiplier }
--
-- 1.0 = normal brightness
-- 0.0 = completely dark
-- 0.2 = barely lit
local flickerPattern = {
    { 0.04, 0.05 },
    { 0.06, 1.00 },
    { 0.03, 0.00 },
    { 0.08, 0.65 },
    { 0.04, 0.00 },
    { 0.03, 1.00 },
    { 0.10, 0.15 },
    { 0.04, 1.00 },
}


function OnStart()
    if not self:HasSpotLight() then
        print("horror_light_flicker: entity has no SpotLight")
        return
    end

    light = self:GetSpotLight()

    -- Preserve whatever intensity you set in the editor.
    normalIntensity = light.intensity

    light.enabled = true
    light.intensity = normalIntensity

    -- Initial delay before the first flicker.
    nextFlicker = 2.5
end


function OnUpdate(dt)
    if light == nil then
        return
    end

    time = time + dt

    ------------------------------------------------
    -- NORMAL / WAITING
    ------------------------------------------------
    if not flickering then
        light.intensity = normalIntensity

        waitTimer = waitTimer + dt

        if waitTimer >= nextFlicker then
            waitTimer = 0.0

            flickering = true
            flickerStep = 1
            flickerTimer = 0.0
        end

        return
    end


    ------------------------------------------------
    -- FLICKER BURST
    ------------------------------------------------
    local step = flickerPattern[flickerStep]

    light.intensity = normalIntensity * step[2]

    flickerTimer = flickerTimer + dt

    if flickerTimer >= step[1] then
        flickerTimer = 0.0
        flickerStep = flickerStep + 1

        -- Finished the burst.
        if flickerStep > #flickerPattern then
            flickering = false

            light.intensity = normalIntensity

            -- Irregular delay before the next burst.
            --
            -- math.sin is available in your scripting environment;
            -- this gives roughly 2-6 seconds between flickers.
            nextFlicker =
                4.0 +
                math.sin(time * 1.73) * 1.2 +
                math.sin(time * 0.47) * 0.8

            if nextFlicker < 1.5 then
                nextFlicker = 1.5
            end
        end
    end
end