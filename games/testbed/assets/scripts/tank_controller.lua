_G.CTF = _G.CTF or {}
CTF.flagCarrier = CTF.flagCarrier or {}
CTF.flagHome = CTF.flagHome or {}
CTF.scores = CTF.scores or {Player1_Tank = 0, Player2_Tank = 0}
CTF.tankSpawn = CTF.tankSpawn or {}
CTF.projectiles = CTF.projectiles or {}
CTF.roundOver = CTF.roundOver or false
CTF.inputLocked = CTF.inputLocked or false

local tankSpeed = 4.0
local carrySpeedFactor = 0.6
local tankTurnSpeed = math.rad(90.0)
local turretTurnSpeed = 90.0
local projectileImpulse = 25.0
local projectileCooldown = 0.5
local projectileHalfExtents = vec3.new(0.12, 0.12, 0.12)

local controlSets = {
    Player1_Tank = {
        throttleForward = Key.W,
        throttleReverse = Key.S,
        turnLeft = Key.A,
        turnRight = Key.D,
        turretLeft = Key.Q,
        turretRight = Key.E,
        fire = Key.Space,
    },
    Player2_Tank = {
        throttleForward = Key.Up,
        throttleReverse = Key.Down,
        turnLeft = Key.Left,
        turnRight = Key.Right,
        turretLeft = Key.Keypad4,
        turretRight = Key.Keypad6,
        fire = Key.RightCtrl,
    },
}

local controls = nil
local rigidBody = nil
local turret = nil
local projectileMaterialSource = nil
local projectileCount = 0
local projectileCooldownRemaining = 0.0
local spawnPosition = nil
local spawnYaw = 0.0
local observedResetSerial = 0

local function readAxis(positive, negative)
    local value = 0.0
    if input:IsKeyHeld(positive) then value = value + 1.0 end
    if input:IsKeyHeld(negative) then value = value - 1.0 end
    return value
end

local function isCarryingFlag()
    local myName = self:GetTag().name
    for _, carrier in pairs(CTF.flagCarrier) do
        if carrier ~= nil and carrier:IsValid() and carrier:HasTag()
            and carrier:GetTag().name == myName then
            return true
        end
    end
    return false
end

local function fireProjectile()
    if turret == nil or not turret:IsValid() then return false end
    if projectileMaterialSource == nil or not projectileMaterialSource:IsValid() then return false end

    local turretWorldTransform = scene:GetWorldTransform(turret)
    local worldAim = turretWorldTransform:TransformDirection(vec3.new(0.0, 1.0, 0.0))
    local horizontalLength = math.sqrt(worldAim.x * worldAim.x + worldAim.z * worldAim.z)
    if horizontalLength < 0.001 then return false end

    local aimDirection = vec3.new(worldAim.x / horizontalLength, 0.0, worldAim.z / horizontalLength)
    local turretPosition = turretWorldTransform:GetTranslation()
    local tankHalfExtents = self:GetCollider().halfExtents
    local tankHorizontalRadius = math.sqrt(
        tankHalfExtents.x * tankHalfExtents.x + tankHalfExtents.z * tankHalfExtents.z)
    local projectileHorizontalRadius = math.sqrt(
        projectileHalfExtents.x * projectileHalfExtents.x
        + projectileHalfExtents.z * projectileHalfExtents.z)
    local spawnDistance = tankHorizontalRadius + projectileHorizontalRadius + 0.1

    projectileCount = projectileCount + 1
    local projectile = scene:CreateEntity(self:GetTag().name .. "_Projectile_" .. projectileCount)
    local projectileTransform = projectile:GetTransform()
    projectileTransform.position = vec3.new(
        turretPosition.x + aimDirection.x * spawnDistance,
        turretPosition.y + 0.15,
        turretPosition.z + aimDirection.z * spawnDistance)

    projectile:AddBoxMesh(projectileHalfExtents)
    projectile:AddMaterial(projectileMaterialSource:GetMaterial().material)
    projectile:AddBoxRigidBody(projectileHalfExtents, BodyMotionType.Dynamic, false, false)
    projectile:AddScript("scripts/projectile.lua")
    table.insert(CTF.projectiles, projectile)

    physics:ApplyImpulse(
        projectile:GetRigidBody().bodyHandle,
        vec3.new(
            aimDirection.x * projectileImpulse,
            0.0,
            aimDirection.z * projectileImpulse))
    return true
end

function OnStart()
    local tankName = self:GetTag().name
    controls = controlSets[tankName]
    rigidBody = self:GetRigidBody()
    turret = scene:FindEntityByName(tankName .. "_Tank_Head_7")
    projectileMaterialSource = scene:FindEntityByName("Arena_Floor")

    local transform = self:GetTransform()
    spawnPosition = vec3.new(transform.position.x, transform.position.y, transform.position.z)
    spawnYaw = transform.rotation.y
    CTF.tankSpawn[tankName] = {
        position = vec3.new(spawnPosition.x, spawnPosition.y, spawnPosition.z),
        yaw = spawnYaw,
    }
    if turret ~= nil and turret:IsValid() then
        local turretRotation = turret:GetTransform().rotation
        CTF.tankSpawn[tankName].turretRotation = vec3.new(
            turretRotation.x, turretRotation.y, turretRotation.z)
    end
    observedResetSerial = CTF.resetSerial

    if controls == nil then
        print("tank_controller: no controls configured for " .. tankName)
    end
    if turret == nil or not turret:IsValid() then
        print("tank_controller: Tank_Head node not found for " .. tankName)
    end
    if projectileMaterialSource == nil or not projectileMaterialSource:IsValid() then
        print("tank_controller: projectile material source not found")
    end
end

function OnUpdate(dt)
    if controls == nil or rigidBody == nil then return end

    if observedResetSerial ~= CTF.resetSerial then
        observedResetSerial = CTF.resetSerial
        projectileCooldownRemaining = 0.0
    end

    if CTF.roundOver or CTF.inputLocked then
        physics:SetBodyVelocity(
            rigidBody.bodyHandle,
            vec3.new(0.0, 0.0, 0.0),
            vec3.new(0.0, 0.0, 0.0))
        return
    end

    projectileCooldownRemaining = math.max(0.0, projectileCooldownRemaining - dt)

    local hullTransform = self:GetTransform()
    local pitch = math.rad(hullTransform.rotation.x)
    local yaw = math.rad(hullTransform.rotation.y)
    -- Tank meshes face local -Z; Transform:GetForward uses local -Y for downward-facing spot lights.
    local forwardX = -math.sin(yaw)
    local forwardZ = -math.cos(pitch) * math.cos(yaw)
    local forwardLength = math.sqrt(forwardX * forwardX + forwardZ * forwardZ)
    local forward = vec3.new(forwardX / forwardLength, 0.0, forwardZ / forwardLength)
    local throttle = readAxis(controls.throttleForward, controls.throttleReverse)
    local turn = readAxis(controls.turnLeft, controls.turnRight)
    local moveSpeed = tankSpeed
    if isCarryingFlag() then
        moveSpeed = moveSpeed * carrySpeedFactor
    end

    physics:SetBodyVelocity(
        rigidBody.bodyHandle,
        vec3.new(forward.x * throttle * moveSpeed, 0.0, forward.z * throttle * moveSpeed),
        vec3.new(0.0, turn * tankTurnSpeed, 0.0))

    if turret ~= nil and turret:IsValid() then
        local turretTurn = readAxis(controls.turretLeft, controls.turretRight)
        local turretTransform = turret:GetTransform()
        -- Tank_Head's local Z, not Y, is the vertical yaw axis here: an axis-correction
        -- rotation baked into the glTF's ancestor node chain maps the Z-up authoring
        -- convention onto the engine's Y-up world, so "spin around Z" in this node's own
        -- local space is a vertical swivel once composed. Confirmed visually, rotation.y
        -- produces a sideways tilt instead.
        turretTransform.rotation.z = turretTransform.rotation.z + turretTurn * turretTurnSpeed * dt
    end

    if projectileCooldownRemaining <= 0.0 and input:IsKeyPressed(controls.fire) then
        if fireProjectile() then
            projectileCooldownRemaining = projectileCooldown
        end
    end
end

function OnFixedUpdate(fixedDt)
end

function OnCollision(otherEntity)
    if CTF.roundOver or CTF.inputLocked then return end
    if otherEntity == nil or not otherEntity:IsValid() or not otherEntity:HasTag() then return end

    local otherName = otherEntity:GetTag().name
    if string.find(otherName, "_Projectile_", 1, true) == nil then return end

    local myName = self:GetTag().name
    if string.find(otherName, myName .. "_Projectile_", 1, true) == 1 then return end

    for flagName, carrier in pairs(CTF.flagCarrier) do
        if carrier ~= nil and carrier:IsValid() and carrier:HasTag()
            and carrier:GetTag().name == myName then
            CTF.flagCarrier[flagName] = nil
        end
    end

    physics:SetBodyPosition(
        rigidBody.bodyHandle,
        spawnPosition,
        vec3.new(0.0, spawnYaw, 0.0))
    physics:SetBodyVelocity(
        rigidBody.bodyHandle,
        vec3.new(0.0, 0.0, 0.0),
        vec3.new(0.0, 0.0, 0.0))
    CTF.lastEvent = myName .. " WAS HIT"
    CTF.eventTimer = 1.5
    camera.Shake({strength = 0.7, duration = 0.3, frequency = 26.0})
    print(self:GetTag().name .. " was hit and respawned")
end
