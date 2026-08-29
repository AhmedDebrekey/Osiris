local character, yaw, pitch = nil, 0.0, 0.0
local mouseSensitivity, moveSpeed = 0.15, 4.0

function OnStart()
    character = self:GetCharacter()

end

function OnUpdate(dt)
    local delta = input:GetMouseDelta()
    yaw = yaw - delta.x * mouseSensitivity
    pitch = math.max(-89.0, math.min(89.0, pitch - delta.y * mouseSensitivity))
    self:GetTransform().rotation = vec3.new(pitch, yaw, 0.0)

    local forward = self:GetTransform():GetForwardXZ()
    local right = self:GetTransform():GetRightXZ()
    local move = vec3.new(0.0, 0.0, 0.0)
    if input:IsKeyHeld(Key.W) then move = move + forward end
    if input:IsKeyHeld(Key.S) then move = move - forward end
    if input:IsKeyHeld(Key.A) then move = move - right end
    if input:IsKeyHeld(Key.D) then move = move + right end

    physics:SetCharacterDesiredVelocity(character.characterHandle,
        move * moveSpeed,
        input:IsKeyPressed(Key.Space))
end
