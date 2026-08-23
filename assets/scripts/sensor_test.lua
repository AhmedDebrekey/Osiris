local function getName(entity)
    if entity ~= nil and entity:IsValid() and entity:HasTag() then
        return entity:GetTag().name
    end
    return "unknown entity"
end

function OnCollision(otherEntity)
    print(self:GetTag().name .. " entered by " .. getName(otherEntity))
end

function OnCollisionEnd(otherEntity)
    print(self:GetTag().name .. " exited by " .. getName(otherEntity))
end
