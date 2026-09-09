function OnStart()
end

function OnUpdate(dt)
end

function OnFixedUpdate(fixedDt)
end

function OnInteract(interactor)
    print("interacted with by " .. interactor:GetTag().name)

    local children = scene:GetChildren(self)
    for _, child in ipairs(children) do
        print("TV child: " .. child:GetTag().name)

        if child:HasSpotLight() then
            local light = child:GetSpotLight()
            light.enabled = not light.enabled
            if light.enabled then
                self:GetInteractable().prompt = "Turn off"
                if _G.ApartmentStory ~= nil then
                    _G.ApartmentStory.message = "No signal."
                    _G.ApartmentStory.messageTimer = 2.2
                end
            else
                self:GetInteractable().prompt = "Turn on"
            end

        end
    end
end
