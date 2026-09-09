local collected = false
local revealed = false
local originalPosition = nil

local function story()
    _G.ApartmentStory = _G.ApartmentStory or {}
    _G.ApartmentStory.checked = _G.ApartmentStory.checked or {}
    return _G.ApartmentStory
end

local function hideKeys()
    local transform = self:GetTransform()
    transform.position = vec3.new(originalPosition.x, -1000.0, originalPosition.z)
end

local function revealKeys()
    revealed = true
    local state = story()
    state.keysRevealed = true
    self:GetTransform().position = vec3.new(originalPosition.x, originalPosition.y, originalPosition.z)
end

function OnStart()
    collected = false
    revealed = false

    local position = self:GetTransform().position
    originalPosition = vec3.new(position.x, position.y, position.z)

    hideKeys()

    if self:HasInteractable() then
        self:GetInteractable().prompt = "Take keys"
    end
end

function OnInteract(interactor)
    local state = story()
    if collected or not revealed or not state.openingComplete or input:IsGameplayInputLocked() then
        return
    end

    collected = true
    state.hasKeys = true
    state.message = "I looked right here."
    state.messageTimer = 4.0

    if self:HasInteractable() then
        self:GetInteractable().prompt = "Taken"
    end

    hideKeys()
    camera.Shake({strength = 0.025, duration = 0.08, frequency = 18.0})
end

function OnUpdate(dt)
    local state = story()
    if not collected and not revealed and state.openingComplete and not state.sleeping
        and state.searchRoute ~= nil and state.searchStep > #state.searchRoute
        and state.messageTimer <= 0.0 then
        revealKeys()
    end
end
