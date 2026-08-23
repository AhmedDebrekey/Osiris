_G.CTF = _G.CTF or {
    flagCarrier = {},
    flagHome = {},
    scores = {
        Player1_Tank = 0,
        Player2_Tank = 0,
    },
}

local winScore = 3
local zoneConfig = {
    P1_Home = {
        ownerTankName = "Player1_Tank",
        enemyFlagName = "P2_Flag",
    },
    P2_Home = {
        ownerTankName = "Player2_Tank",
        enemyFlagName = "P1_Flag",
    },
}

local config = nil

function OnStart()
    local zoneName = self:GetTag().name
    config = zoneConfig[zoneName]
    if config == nil then
        print("home_zone: no configuration for " .. zoneName)
    end
end

function OnCollision(otherEntity)
    if config == nil then return end
    if otherEntity == nil or not otherEntity:IsValid() or not otherEntity:HasTag() then return end
    if otherEntity:GetTag().name ~= config.ownerTankName then return end
    if CTF.flagCarrier[config.enemyFlagName] ~= otherEntity then return end

    CTF.scores[config.ownerTankName] = CTF.scores[config.ownerTankName] + 1
    local score = CTF.scores[config.ownerTankName]
    print(config.ownerTankName .. " captured " .. config.enemyFlagName .. ". Score: " .. score)

    CTF.flagCarrier[config.enemyFlagName] = nil
    local enemyFlag = scene:FindEntityByName(config.enemyFlagName)
    local homePosition = CTF.flagHome[config.enemyFlagName]
    if enemyFlag ~= nil and enemyFlag:IsValid() and homePosition ~= nil then
        enemyFlag:GetTransform().position = homePosition
    end

    if score >= winScore then
        print("*** " .. config.ownerTankName .. " WINS WITH " .. score .. " CAPTURES! ***")
    end
end
