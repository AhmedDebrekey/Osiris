_G.CTF = _G.CTF or {}
CTF.flagCarrier = CTF.flagCarrier or {}
CTF.flagHome = CTF.flagHome or {}
CTF.flagState = CTF.flagState or {}
CTF.scores = CTF.scores or {}
CTF.tankSpawn = CTF.tankSpawn or {}
CTF.projectiles = CTF.projectiles or {}
CTF.winScore = 3
CTF.roundOver = CTF.roundOver or false
CTF.inputLocked = CTF.inputLocked or false
CTF.resetSerial = CTF.resetSerial or 0
CTF.roundNumber = CTF.roundNumber or 1

local fadeAlpha = 0.0
local transition = "idle"
local resetApplied = false
local fadeSpeed = 2.5

local function entityName(entity)
    if entity ~= nil and entity:IsValid() and entity:HasTag() then
        return entity:GetTag().name
    end
    return nil
end

local function displayPlayerName(tankName)
    if tankName == "Player1_Tank" then return "PLAYER 1" end
    if tankName == "Player2_Tank" then return "PLAYER 2" end
    return tankName or "UNKNOWN"
end

local function flagStatus(flagName)
    local carrierName = entityName(CTF.flagCarrier[flagName])
    if carrierName ~= nil then
        return "TAKEN BY " .. displayPlayerName(carrierName)
    end

    local state = CTF.flagState[flagName]
    if state == "DROPPED" then return "DROPPED" end
    return "HOME"
end

local function drawHud()
    ui.Rect({x = 0.02, y = 0.025, w = 0.31, h = 0.115, anchor = "topleft", color = {0.02, 0.08, 0.18, 0.82}})
    ui.Rect({x = 0.67, y = 0.025, w = 0.31, h = 0.115, anchor = "topleft", color = {0.18, 0.04, 0.03, 0.82}})

    ui.Text({
        x = 0.045, y = 0.045, anchor = "topleft",
        text = "PLAYER 1    " .. tostring(CTF.scores.Player1_Tank or 0),
        color = {0.55, 0.78, 1.0, 1.0}, size = 30,
    })
    ui.Text({
        x = 0.955, y = 0.045, anchor = "topright",
        text = tostring(CTF.scores.Player2_Tank or 0) .. "    PLAYER 2",
        color = {1.0, 0.58, 0.48, 1.0}, size = 30,
    })

    ui.Text({
        x = 0.045, y = 0.098, anchor = "topleft",
        text = "FLAG: " .. flagStatus("P1_Flag"),
        color = {0.78, 0.88, 1.0, 1.0}, size = 18,
    })
    ui.Text({
        x = 0.955, y = 0.098, anchor = "topright",
        text = "FLAG: " .. flagStatus("P2_Flag"),
        color = {1.0, 0.80, 0.74, 1.0}, size = 18,
    })

    ui.Text({
        x = 0.02, y = 0.975, anchor = "bottomleft",
        text = "P1  WASD move   Q/E turret   SPACE fire",
        color = {0.80, 0.88, 1.0, 0.9}, size = 17,
    })
    ui.Text({
        x = 0.98, y = 0.975, anchor = "bottomright",
        text = "P2  ARROWS move   KP4/KP6 turret   RIGHT CTRL fire",
        color = {1.0, 0.82, 0.76, 0.9}, size = 17,
    })

    if CTF.eventTimer ~= nil and CTF.eventTimer > 0.0 and CTF.lastEvent ~= nil then
        ui.Text({
            x = 0.5, y = 0.17, anchor = "topcenter",
            text = CTF.lastEvent,
            color = {1.0, 0.92, 0.55, 1.0}, size = 24,
        })
    end
end

local function resetRound()
    for _, projectile in ipairs(CTF.projectiles or {}) do
        if projectile ~= nil and projectile:IsValid() then
            scene:DestroyEntity(projectile)
        end
    end
    CTF.projectiles = {}

    for tankName, spawn in pairs(CTF.tankSpawn or {}) do
        local tank = scene:FindEntityByName(tankName)
        if tank ~= nil and tank:IsValid() and tank:HasRigidBody() then
            local body = tank:GetRigidBody().bodyHandle
            physics:SetBodyPosition(body, spawn.position, vec3.new(0.0, spawn.yaw, 0.0))
            physics:SetBodyVelocity(body, vec3.new(0.0), vec3.new(0.0))
        end

        local turret = scene:FindEntityByName(tankName .. "_Tank_Head_7")
        if turret ~= nil and turret:IsValid() and spawn.turretRotation ~= nil then
            turret:GetTransform().rotation = vec3.new(
                spawn.turretRotation.x,
                spawn.turretRotation.y,
                spawn.turretRotation.z)
        end
    end

    CTF.flagCarrier = {}
    for flagName, homePosition in pairs(CTF.flagHome or {}) do
        local flag = scene:FindEntityByName(flagName)
        if flag ~= nil and flag:IsValid() then
            flag:GetTransform().position = vec3.new(homePosition.x, homePosition.y, homePosition.z)
        end
        CTF.flagState[flagName] = "HOME"
    end

    CTF.scores.Player1_Tank = 0
    CTF.scores.Player2_Tank = 0
    CTF.winner = nil
    CTF.roundOver = false
    CTF.resetSerial = CTF.resetSerial + 1
    CTF.roundNumber = CTF.roundNumber + 1
    CTF.lastEvent = "ROUND " .. tostring(CTF.roundNumber)
    CTF.eventTimer = 2.0
end

local function drawWinScreen()
    ui.Rect({x = 0.0, y = 0.0, w = 1.0, h = 1.0, anchor = "topleft", color = {0.0, 0.0, 0.0, 0.72}})
    ui.Text({
        x = 0.5, y = 0.39, anchor = "center",
        text = displayPlayerName(CTF.winner) .. " WINS",
        color = {1.0, 0.88, 0.40, 1.0}, size = 58,
    })
    ui.Text({
        x = 0.5, y = 0.50, anchor = "center",
        text = tostring(CTF.winScore) .. " CAPTURES",
        color = {1.0, 1.0, 1.0, 1.0}, size = 28,
    })
    ui.Text({
        x = 0.5, y = 0.62, anchor = "center",
        text = "PRESS R FOR A NEW ROUND",
        color = {0.82, 0.86, 0.92, 1.0}, size = 24,
    })
end

function OnStart()
    CTF.flagCarrier = {}
    CTF.flagState.P1_Flag = "HOME"
    CTF.flagState.P2_Flag = "HOME"
    CTF.scores.Player1_Tank = 0
    CTF.scores.Player2_Tank = 0
    CTF.projectiles = {}
    CTF.winner = nil
    CTF.roundOver = false
    CTF.inputLocked = false
    CTF.roundNumber = 1
    CTF.resetSerial = CTF.resetSerial + 1
    CTF.lastEvent = "CAPTURE 3 FLAGS TO WIN"
    CTF.eventTimer = 3.0
end

function OnUpdate(dt)
    if CTF.eventTimer ~= nil then
        CTF.eventTimer = math.max(0.0, CTF.eventTimer - dt)
    end

    drawHud()

    if CTF.roundOver and transition == "idle" then
        drawWinScreen()
        if input:IsKeyPressed(Key.R) then
            transition = "fadeOut"
            resetApplied = false
            CTF.inputLocked = true
        end
    end

    if transition == "fadeOut" then
        fadeAlpha = MoveTowards(fadeAlpha, 1.0, dt * fadeSpeed)
        if fadeAlpha >= 1.0 and not resetApplied then
            resetRound()
            resetApplied = true
            transition = "fadeIn"
        end
    elseif transition == "fadeIn" then
        fadeAlpha = MoveTowards(fadeAlpha, 0.0, dt * fadeSpeed)
        if fadeAlpha <= 0.0 then
            transition = "idle"
            CTF.inputLocked = false
        end
    end

    if fadeAlpha > 0.0 then
        ui.FadeToBlack(fadeAlpha)
    end
end

function OnFixedUpdate(fixedDt)
end
