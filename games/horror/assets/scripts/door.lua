local leaving = false

local function story()
    _G.ApartmentStory = _G.ApartmentStory or {}
    _G.ApartmentStory.checked = _G.ApartmentStory.checked or {}
    return _G.ApartmentStory
end

function OnStart()
    leaving = false

    if self:HasInteractable() then
        self:GetInteractable().prompt = "Try door"
    end
end

function OnInteract(interactor)
    if leaving then
        return
    end

    local state = story()
    if not state.hasKeys then
        state.message = "Locked."
        state.messageTimer = 2.8
        camera.Shake({strength = 0.04, duration = 0.10, frequency = 20.0})
        return
    end

    leaving = true
    state.message = ""
    state.messageTimer = 0.0
    state.leftApartment = true

    if self:HasInteractable() then
        self:GetInteractable().prompt = "Leaving..."
    end
end
