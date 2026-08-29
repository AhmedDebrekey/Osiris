_G.CTF = _G.CTF or {}
CTF.flagCarrier = CTF.flagCarrier or {}
CTF.flagHome = CTF.flagHome or {}
CTF.flagState = CTF.flagState or {}
CTF.scores = CTF.scores or {Player1_Tank = 0, Player2_Tank = 0}
CTF.resetSerial = CTF.resetSerial or 0

local flagCarryOffset = 0.9
local flagPickupDistance = 1.4
local dropPickupCooldown = 0.5
local atHomeDistance = 0.1
local flagConfig = {
    P1_Flag = {
        ownerTankName = "Player1_Tank",
        ownerLabel = "PLAYER 1",
        enemyTankName = "Player2_Tank",
        homeZoneName = "P1_Home",
    },
    P2_Flag = {
        ownerTankName = "Player2_Tank",
        ownerLabel = "PLAYER 2",
        enemyTankName = "Player1_Tank",
        homeZoneName = "P2_Home",
    },
}

local myName = nil
local config = nil
local homePosition = nil
local lastCarrier = nil
local pickupCooldownRemaining = 0.0
local observedResetSerial = 0

local function isAtHome(flagPosition)
    local hx = flagPosition.x - homePosition.x
    local hz = flagPosition.z - homePosition.z
    return hx * hx + hz * hz <= atHomeDistance * atHomeDistance
end

local function tryPickup(tank)
    if config == nil or CTF.flagCarrier[myName] ~= nil then return false end
    if tank == nil or not tank:IsValid() or not tank:HasTag() then return false end

    local flagPosition = self:GetTransform().position
    -- At home, only the enemy taking it counts as a steal. Once dropped in the field, either
    -- side can recover it, per the earlier "dropped flag: either player can grab it" decision.
    local tankName = tank:GetTag().name
    if isAtHome(flagPosition) and tankName ~= config.enemyTankName then return false end

    local tankPosition = tank:GetTransform().position
    local dx = flagPosition.x - tankPosition.x
    local dz = flagPosition.z - tankPosition.z
    if dx * dx + dz * dz > flagPickupDistance * flagPickupDistance then return false end

    if tankName == config.ownerTankName then
        self:GetTransform().position = vec3.new(homePosition.x, homePosition.y, homePosition.z)
        CTF.flagCarrier[myName] = nil
        CTF.flagState[myName] = "HOME"
        lastCarrier = nil
        pickupCooldownRemaining = dropPickupCooldown
        CTF.lastEvent = config.ownerLabel .. " RETURNED ITS FLAG"
        CTF.eventTimer = 2.0
        return true
    end

    -- Collision arguments are transient references; re-resolve a value before retaining it.
    lastCarrier = scene:FindEntityByName(tankName)
    CTF.flagCarrier[myName] = lastCarrier
    CTF.flagState[myName] = "CARRIED"
    CTF.lastEvent = tankName .. " STOLE " .. myName
    CTF.eventTimer = 2.0
    print(tank:GetTag().name .. " picked up " .. myName)
    return true
end

function OnStart()
    myName = self:GetTag().name
    config = flagConfig[myName]

    local position = self:GetTransform().position
    homePosition = vec3.new(position.x, position.y, position.z)
    CTF.flagHome[myName] = homePosition
    CTF.flagState[myName] = "HOME"
    observedResetSerial = CTF.resetSerial

    if config == nil then
        print("flag: no configuration for " .. myName)
    end
end

function OnUpdate(dt)
    if config == nil then return end

    if observedResetSerial ~= CTF.resetSerial then
        observedResetSerial = CTF.resetSerial
        CTF.flagCarrier[myName] = nil
        CTF.flagState[myName] = "HOME"
        lastCarrier = nil
        pickupCooldownRemaining = 0.0
        self:GetTransform().position = vec3.new(homePosition.x, homePosition.y, homePosition.z)
    end

    if CTF.roundOver or CTF.inputLocked then return end

    local carrier = CTF.flagCarrier[myName]
    if carrier == nil then
        CTF.flagState[myName] = isAtHome(self:GetTransform().position) and "HOME" or "DROPPED"
        if lastCarrier ~= nil then
            lastCarrier = nil
            pickupCooldownRemaining = dropPickupCooldown
        elseif pickupCooldownRemaining > 0.0 then
            pickupCooldownRemaining = math.max(0.0, pickupCooldownRemaining - dt)
        elseif not tryPickup(scene:FindEntityByName(config.enemyTankName)) then
            -- Only the enemy can steal it from home; once dropped, either side can recover it,
            -- so poll the owner's tank too (tryPickup itself still enforces the at-home rule).
            tryPickup(scene:FindEntityByName(config.ownerTankName))
        end
        return
    end

    lastCarrier = carrier

    local carrierPosition = carrier:GetTransform().position
    self:GetTransform().position = vec3.new(
        carrierPosition.x,
        carrierPosition.y + flagCarryOffset,
        carrierPosition.z)
end

function OnCollision(otherEntity)
    if CTF.roundOver or CTF.inputLocked then return end
    if pickupCooldownRemaining > 0.0 then return end
    tryPickup(otherEntity)
end
