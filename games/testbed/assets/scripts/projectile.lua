local lifetime = 5.0

function OnUpdate(dt)
    lifetime = lifetime - dt
    if lifetime <= 0.0 then
        scene:DestroyEntity(self)
    end
end

function OnCollision(otherEntity)
    local otherName = "unknown entity"
    if otherEntity ~= nil and otherEntity:IsValid() and otherEntity:HasTag() then
        otherName = otherEntity:GetTag().name
    end

    print(self:GetTag().name .. " hit " .. otherName)
    scene:DestroyEntity(self)
end
