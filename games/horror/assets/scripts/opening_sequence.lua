local elapsed = 0.0
local endingFade = 0.0
local endingElapsed = 0.0
local ambienceStopped = false
local wakeShakePlayed = false
local cutsceneFinished = false
local playerEntity = nil

local wakeStart = 21.0
local wakeEnd = wakeStart + 3.2
local wakePosition = vec3.new(-3.55, 1.18, 9.62)
local wakeRotation = vec3.new(72.0, -18.0, 0.0)

local openingLines = {
    { startTime = 1.0, endTime = 5.5, text = "Child: Mum, Teddy needs the blue plaster." },
    { startTime = 6.1, endTime = 10.6, text = "Mother: Again? That's his third this week." },
    { startTime = 11.2, endTime = 14.0, text = "Child: I'm practising." },
    { startTime = 14.6, endTime = 17.1, text = "Mother: For what?" },
    { startTime = 17.7, endTime = 21.0, text = "Child: For when I'm a doctor." },
}

local function story()
    _G.ApartmentStory = _G.ApartmentStory or {}
    _G.ApartmentStory.checked = _G.ApartmentStory.checked or {}
    return _G.ApartmentStory
end

local function drawCenteredLine(text, alpha)
    ui.Rect({
        x = 0.5, y = 0.80, w = 0.72, h = 0.07, anchor = "center",
        color = {0.0, 0.0, 0.0, 0.55 * alpha},
    })
    ui.Text({
        x = 0.5, y = 0.80, anchor = "center",
        text = text,
        color = {0.94, 0.94, 0.90, alpha},
        size = 23,
    })
end

local function drawBlack(alpha)
    ui.Rect({
        x = 0.0, y = 0.0, w = 1.0, h = 1.0, anchor = "topleft",
        color = {0.0, 0.0, 0.0, alpha},
    })
end

local function drawObjective(text)
    ui.Text({
        x = 0.035, y = 0.94, anchor = "bottomleft",
        text = text,
        color = {0.90, 0.90, 0.84, 0.88},
        size = 19,
    })
end

local function drawOpeningLine()
    for _, line in ipairs(openingLines) do
        if elapsed >= line.startTime and elapsed < line.endTime then
            local fadeIn = math.min(1.0, (elapsed - line.startTime) * 3.0)
            local fadeOut = math.min(1.0, (line.endTime - elapsed) * 3.0)
            drawCenteredLine(line.text, math.min(fadeIn, fadeOut))
            return
        end
    end
end

local function lerp(a, b, t)
    return a + (b - a) * t
end

local function finishCutscene(state)
    if cutsceneFinished then
        return
    end

    cutsceneFinished = true
    if self:HasCamera() then
        self:GetCamera().isPrimary = false
    end
    if playerEntity ~= nil and playerEntity:IsValid() and playerEntity:HasCamera() then
        playerEntity:GetCamera().isPrimary = true
    end

    state.openingComplete = true
    state.controlsLocked = false
    input:SetGameplayInputLocked(false)
    ui.FadeToBlack(0.0)
end

local function advanceOpening()
    for index, line in ipairs(openingLines) do
        if elapsed < line.endTime then
            if elapsed < line.startTime then
                elapsed = line.startTime
            else
                elapsed = openingLines[index + 1] and openingLines[index + 1].startTime or wakeStart
            end
            return
        end
    end

    elapsed = wakeEnd
end

local function drawSkipHint()
    ui.Text({
        x = 0.97, y = 0.96, anchor = "bottomright",
        text = elapsed < wakeStart and "Esc: next line" or "Esc: skip waking up",
        color = {0.70, 0.70, 0.66, 0.8},
        size = 16,
    })
end

local function updateOpening(dt, state)
    if input:IsKeyPressed(Key.Escape) then
        advanceOpening()
    else
        elapsed = elapsed + dt
    end

    if elapsed < wakeStart then
        drawBlack(1.0)
        drawSkipHint()
        drawOpeningLine()
        return
    end

    if not wakeShakePlayed then
        wakeShakePlayed = true
        camera.Shake({strength = 0.025, duration = 0.18, frequency = 13.0})
    end

    local progress = math.min(1.0, (elapsed - wakeStart) / (wakeEnd - wakeStart))
    local eased = progress * progress * (3.0 - 2.0 * progress)
    local moveProgress = math.min(1.0, (elapsed - wakeStart) / 0.9)
    local moveEased = moveProgress * moveProgress * (3.0 - 2.0 * moveProgress)
    local playerTransform = playerEntity:GetTransform()
    local playerCamera = playerEntity:GetCamera()
    local targetPosition = playerTransform.position + vec3.new(0.0, playerCamera.eyeHeight, 0.0)
    local targetRotation = playerTransform.rotation
    local transform = self:GetTransform()
    transform.position = vec3.new(
        lerp(wakePosition.x, targetPosition.x, moveEased),
        lerp(wakePosition.y, targetPosition.y, moveEased),
        lerp(wakePosition.z, targetPosition.z, moveEased))
    transform.rotation = vec3.new(
        lerp(wakeRotation.x, targetRotation.x, eased),
        lerp(wakeRotation.y, targetRotation.y, eased),
        0.0)

    ui.FadeToBlack(math.max(0.0, 1.0 - (elapsed - wakeStart) / 1.35))

    if progress >= 1.0 then
        finishCutscene(state)
    else
        drawSkipHint()
    end
end

function OnStart()
    _G.ApartmentStory = {
        checked = {},
        openingComplete = false,
        controlsLocked = true,
        keysRevealed = false,
        hasKeys = false,
        sleeping = false,
        leftApartment = false,
        message = "",
        messageTimer = 0.0,
        searchStep = 1,
        searchRoute = {
            { entity = "kitchencounter", objective = "Check the kitchen counter",
              response = "Not beside the fruit bowl. Did I leave them by the door?" },
            { entity = "coat_hanger", objective = "Check the coat stand by the front door",
              response = "Not on the hook. I was sitting on the sofa last night." },
            { entity = "Couch", objective = "Look between the sofa cushions",
              response = "Just loose change. I'll check the counter once more." },
        },
    }

    elapsed = 0.0
    endingFade = 0.0
    endingElapsed = 0.0
    ambienceStopped = false
    wakeShakePlayed = false
    cutsceneFinished = false
    playerEntity = scene:FindEntityByName("ExteriorPreviewCamera")

    input:SetGameplayInputLocked(true)
    if self:HasCamera() then
        self:GetCamera().isPrimary = true
        self:GetTransform().position = wakePosition
        self:GetTransform().rotation = wakeRotation
    end
    if playerEntity ~= nil and playerEntity:IsValid() and playerEntity:HasCamera() then
        playerEntity:GetCamera().isPrimary = false
    end
end

function OnUpdate(dt)
    local state = story()

    if not state.openingComplete then
        if playerEntity == nil or not playerEntity:IsValid() or not playerEntity:HasCamera() then
            finishCutscene(state)
        else
            updateOpening(dt, state)
        end
        return
    end

    if state.leftApartment then
        if not ambienceStopped and self:HasAudioSource() then
            audio:StopSource(self:GetAudioSource().sourceHandle)
            ambienceStopped = true
        end

        endingElapsed = endingElapsed + dt
        endingFade = MoveTowards(endingFade, 1.0, dt * 0.85)
        drawBlack(endingFade)

        if endingElapsed > 3.0 then
            ui.Text({
                x = 0.5, y = 0.54, anchor = "center",
                text = "End of current build",
                color = {0.66, 0.66, 0.62, math.min(1.0, endingElapsed - 3.0)},
                size = 18,
            })
        end
        return
    end

    if state.sleeping then
        return
    end

    if state.messageTimer > 0.0 then
        state.messageTimer = math.max(0.0, state.messageTimer - dt)
        drawCenteredLine(state.message, math.min(1.0, state.messageTimer * 2.0))
    end

    if state.hasKeys then
        drawObjective("Leave the apartment")
    elseif state.searchStep <= #state.searchRoute then
        drawObjective(state.searchRoute[state.searchStep].objective)
    else
        drawObjective("Check the kitchen counter again")
    end
end
