_G.CTF = _G.CTF or {
    flagCarrier = {},
    flagHome = {},
    scores = {
        Player1_Tank = 0,
        Player2_Tank = 0,
    },
}

local flagCarryOffset = 0.9
local flagPickupDistance = 1.4
local dropPickupCooldown = 0.5
local atHomeDistance = 0.1
local flagConfig = {
    P1_Flag = {
        ownerTankName = "Player1_Tank",
        enemyTankName = "Player2_Tank",
        homeZoneName = "P1_Home",
    },
    P2_Flag = {
        ownerTankName = "Player2_Tank",
        enemyTankName = "Player1_Tank",
        homeZoneName = "P2_Home",
    },
}

local myName = nil
local config = nil
local homePosition = nil
local lastCarrier = nil
local pickupCooldownRemaining = 0.0

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
    if isAtHome(flagPosition) and tank:GetTag().name ~= config.enemyTankName then return false end

    local tankPosition = tank:GetTransform().position
    local dx = flagPosition.x - tankPosition.x
    local dz = flagPosition.z - tankPosition.z
    if dx * dx + dz * dz > flagPickupDistance * flagPickupDistance then return false end

    -- Collision arguments are transient references; re-resolve a value before retaining it.
    lastCarrier = scene:FindEntityByName(tank:GetTag().name)
    CTF.flagCarrier[myName] = lastCarrier
    print(tank:GetTag().name .. " picked up " .. myName)
    return true
end

function OnStart()
    myName = self:GetTag().name
    config = flagConfig[myName]

    local position = self:GetTransform().position
    homePosition = vec3.new(position.x, position.y, position.z)
    CTF.flagHome[myName] = homePosition

    if config == nil then
        print("flag: no configuration for " .. myName)
    end
end

function OnUpdate(dt)
    if config == nil then return end

    local carrier = CTF.flagCarrier[myName]
    if carrier == nil then
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
    if pickupCooldownRemaining > 0.0 then return end
    tryPickup(otherEntity)
end
