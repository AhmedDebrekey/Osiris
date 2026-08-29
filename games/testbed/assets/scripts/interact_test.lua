-- Proves OnInteract fires: attach this alongside an Interactable component and press
-- whichever key that component's keyCode is set to while looking at the entity.

function OnStart()
    print("interact_test: started on " .. self:GetTag().name)
end

function OnUpdate(dt)
end

function OnFixedUpdate(fixedDt)
end

function OnInteract(interactor)
    print("interact_test: interacted with by " .. interactor:GetTag().name)
end
