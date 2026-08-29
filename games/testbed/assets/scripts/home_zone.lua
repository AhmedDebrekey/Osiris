_G.CTF = _G.CTF or {}
CTF.flagCarrier = CTF.flagCarrier or {}
CTF.flagHome = CTF.flagHome or {}
CTF.flagState = CTF.flagState or {}
CTF.scores = CTF.scores or {Player1_Tank = 0, Player2_Tank = 0}
CTF.winScore = CTF.winScore or 3
CTF.roundOver = CTF.roundOver or false

local zoneConfig = {
    P1_Home = {
        ownerTankName = "Player1_Tank",
        ownerLabel = "PLAYER 1",
        ownFlagName = "P1_Flag",
        enemyFlagName = "P2_Flag",
    },
    P2_Home = {
        ownerTankName = "Player2_Tank",
        ownerLabel = "PLAYER 2",
        ownFlagName = "P2_Flag",
        enemyFlagName = "P1_Flag",
    },
}

local config = nil
local ownerInside = false

function OnStart()
    local zoneName = self:GetTag().name
    config = zoneConfig[zoneName]
    if config == nil then
        print("home_zone: no configuration for " .. zoneName)
    end
end

-- Shared by OnCollision (new contact) and OnUpdate (held contact), so a carrier already
-- standing in the zone still scores once its own flag returns home instead of needing to
-- leave and re-enter the trigger volume to get re-evaluated.
local function tryScore()
    if config == nil then return end
    if CTF.roundOver or CTF.inputLocked then return end

    local carrier = CTF.flagCarrier[config.enemyFlagName]
    if carrier == nil or not carrier:IsValid() or not carrier:HasTag() then return end
    if carrier:GetTag().name ~= config.ownerTankName then return end
    if CTF.flagState[config.ownFlagName] ~= "HOME" then
        CTF.lastEvent = config.ownerLabel .. " CANNOT SCORE WHILE ITS FLAG IS AWAY"
        CTF.eventTimer = 2.0
        return
    end

    CTF.scores[config.ownerTankName] = CTF.scores[config.ownerTankName] + 1
    local score = CTF.scores[config.ownerTankName]
    print(config.ownerTankName .. " captured " .. config.enemyFlagName .. ". Score: " .. score)

    CTF.flagCarrier[config.enemyFlagName] = nil
    CTF.flagState[config.enemyFlagName] = "HOME"
    local enemyFlag = scene:FindEntityByName(config.enemyFlagName)
    local homePosition = CTF.flagHome[config.enemyFlagName]
    if enemyFlag ~= nil and enemyFlag:IsValid() and homePosition ~= nil then
        enemyFlag:GetTransform().position = homePosition
    end

    CTF.lastEvent = config.ownerLabel .. " CAPTURED THE FLAG"
    CTF.eventTimer = 2.5
    camera.Shake({strength = 0.45, duration = 0.35, frequency = 18.0})

    if score >= CTF.winScore then
        CTF.winner = config.ownerTankName
        CTF.roundOver = true
        CTF.inputLocked = true
        print("*** " .. config.ownerTankName .. " WINS WITH " .. score .. " CAPTURES! ***")
    end
end

function OnCollision(otherEntity)
    if config == nil then return end
    if otherEntity == nil or not otherEntity:IsValid() or not otherEntity:HasTag() then return end
    if otherEntity:GetTag().name ~= config.ownerTankName then return end

    ownerInside = true
    tryScore()
end

function OnCollisionEnd(otherEntity)
    if config == nil then return end
    if otherEntity == nil or not otherEntity:IsValid() or not otherEntity:HasTag() then return end
    if otherEntity:GetTag().name ~= config.ownerTankName then return end

    ownerInside = false
end

function OnUpdate(dt)
    if ownerInside then
        tryScore()
    end
end
