-- Self-manipulation test: rotates the entity this script is attached to.
-- Proves OnStart/OnUpdate/OnFixedUpdate all run and that self:GetTransform()
-- returns a live reference back into the entity's own TransformComponent.

function OnStart()
    print("spin_cube: started on " .. self:GetTag().name)
end

function OnUpdate(dt)
    local transform = self:GetTransform()
    transform.rotation.y = transform.rotation.y + dt * 45.0
end

function OnFixedUpdate(fixedDt)
end
