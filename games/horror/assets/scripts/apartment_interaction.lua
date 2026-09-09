local responses = {
    Couch = "Two coins caught in the lining.",
    toilet_sink = "Only soap and a wet comb.",
    toilet_seat = "Nothing here.",
    alarm_clock = "3:07. It has stopped.",
    closet = "One clean shirt.",
    fridge = "Milk, mustard, half an onion.",
    oven = "Cold.",
    kitchencounter = "This is where I usually leave them.",
    coat_hanger = "Nothing hanging on the hook.",
    bathtub = "The tap is dripping again.",
    bottled_car = "The front wheel has come loose inside the bottle.",
}

local placeNames = {
    Couch = "Couch",
    toilet_sink = "Sink",
    toilet_seat = "Toilet",
    closet = "Closet",
    fridge = "Fridge",
    kitchencounter = "Counter",
}

local function story()
    _G.ApartmentStory = _G.ApartmentStory or {}
    _G.ApartmentStory.checked = _G.ApartmentStory.checked or {}
    return _G.ApartmentStory
end

function OnStart()
end

function OnInteract(interactor)
    local name = self:GetTag().name
    local state = story()
    if not state.openingComplete or input:IsGameplayInputLocked() then return end
    state.message = responses[name] or "Nothing useful here."
    state.messageTimer = 4.5

    local step = state.searchRoute and state.searchRoute[state.searchStep]
    if not state.hasKeys and step ~= nil and step.entity == name then
        state.message = step.response
        state.messageTimer = 5.0
        state.searchStep = state.searchStep + 1
    elseif name == "kitchencounter" then
        if state.hasKeys then
            state.message = "I checked right here."
        elseif state.keysRevealed then
            state.message = "There they are."
        else
            state.message = "Nothing beside the fruit bowl."
        end
    end

    local placeName = placeNames[name]
    if placeName ~= nil then
        state.checked[placeName] = true
    end
end
