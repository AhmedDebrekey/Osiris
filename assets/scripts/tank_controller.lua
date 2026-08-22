local tankSpeed = 4.0
local tankTurnSpeed = math.rad(90.0)
local turretTurnSpeed = 90.0

local controlSets = {
    Player1_Tank = {
        throttleForward = Key.W,
        throttleReverse = Key.S,
        turnLeft = Key.A,
        turnRight = Key.D,
        turretLeft = Key.Q,
        turretRight = Key.E,
    },
    Player2_Tank = {
        throttleForward = Key.Up,
        throttleReverse = Key.Down,
        turnLeft = Key.Left,
        turnRight = Key.Right,
        turretLeft = Key.Keypad4,
        turretRight = Key.Keypad6,
    },
}

local controls = nil
local rigidBody = nil
local turret = nil

local function readAxis(positive, negative)
    local value = 0.0
    if input:IsKeyHeld(positive) then value = value + 1.0 end
    if input:IsKeyHeld(negative) then value = value - 1.0 end
    return value
end

function OnStart()
    local tankName = self:GetTag().name
    controls = controlSets[tankName]
    rigidBody = self:GetRigidBody()
    turret = scene:FindEntityByName(tankName .. "_Tank_Head_7")

    if controls == nil then
        print("tank_controller: no controls configured for " .. tankName)
    end
    if turret == nil or not turret:IsValid() then
        print("tank_controller: Tank_Head node not found for " .. tankName)
    end
end

function OnUpdate(dt)
    if controls == nil or rigidBody == nil then return end

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

    physics:SetBodyVelocity(
        rigidBody.bodyHandle,
        vec3.new(forward.x * throttle * tankSpeed, 0.0, forward.z * throttle * tankSpeed),
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
end

function OnFixedUpdate(fixedDt)
end
