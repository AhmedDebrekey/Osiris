local directory = 'games/horror/assets/scripts/'
local vector = {}
vector.__index = vector
vector.__add = function(a,b) return vec3.new(a.x+b.x,a.y+b.y,a.z+b.z) end
vector.__sub = function(a,b) return vec3.new(a.x-b.x,a.y-b.y,a.z-b.z) end
vector.__mul = function(a,b) return vec3.new(a.x*b,a.y*b,a.z*b) end
vec3 = {new=function(x,y,z) return setmetatable({x=x,y=y,z=z},vector) end}
function MoveTowards(a,b,d) return a < b and math.min(a+d,b) or math.max(a-d,b) end
local locked = false
local escapePressed = false
input = {
    SetGameplayInputLocked=function(_,value) locked=value end,
    IsGameplayInputLocked=function() return locked end,
    GetMouseDelta=function() return {x=50,y=50} end,
    IsKeyHeld=function() return true end,
    IsKeyPressed=function(_,key) return key == Key.Escape and escapePressed end,
}
Key={W=1,A=2,S=3,D=4,Space=5,Escape=6}
local calls = {}
local fadeAlpha = 0
ui = {
    Text=function(options) calls[#calls+1]={'text',options.text} end,
    Rect=function() calls[#calls+1]={'rect'} end,
    FadeToBlack=function(alpha) fadeAlpha=alpha; calls[#calls+1]={'overlay'} end,
}
camera={Shake=function() end}
audio={StopSource=function() end}
local velocity, jump
physics={SetCharacterDesiredVelocity=function(_,handle,v,j) velocity=v; jump=j end}
local function entity(name,y)
    local transform={position=vec3.new(5.3,y or 0,8.3),rotation=vec3.new(0,0,0)}
    function transform:GetForwardXZ() return vec3.new(0,0,-1) end
    function transform:GetRightXZ() return vec3.new(1,0,0) end
    local cameraComponent={eyeHeight=1.7,isPrimary=name=='ExteriorPreviewCamera'}
    local interactable={prompt='Inspect'}
    return {
        IsValid=function() return true end,
        GetTransform=function() return transform end,
        HasCamera=function() return true end,
        GetCamera=function() return cameraComponent end,
        GetCharacter=function() return {characterHandle=1} end,
        GetTag=function() return {name=name} end,
        HasInteractable=function() return true end,
        GetInteractable=function() return interactable end,
        HasAudioSource=function() return false end,
    }
end
local player=entity('ExteriorPreviewCamera')
scene={FindEntityByName=function() return player end}
local function script(file,owner)
    local env=setmetatable({self=owner},{__index=_G})
    assert(loadfile(directory..file,'t',env))()
    return env
end
local controller=script('fps_controller.lua',player)
local opening=script('opening_sequence.lua',entity('OpeningSequence'))
local keyEntity=entity('keys',1.508)
local keys=script('keys.lua',keyEntity)
local counter=script('apartment_interaction.lua',entity('kitchencounter'))
local coat=script('apartment_interaction.lua',entity('coat_hanger'))
local sofa=script('apartment_interaction.lua',entity('Couch'))
local door=script('door.lua',entity('front_door'))
for _,filename in ipairs({'opening_sequence.lua','keys.lua','apartment_interaction.lua','door.lua','sleep.lua','fps_controller.lua','tv_script.lua'}) do
    assert(loadfile(directory..filename))
end
for run=1,2 do
    keyEntity:GetTransform().position=vec3.new(5.3,1.508,8.3)
    if run==1 then keys.OnStart(); opening.OnStart() else opening.OnStart(); keys.OnStart() end
    controller.OnStart()
    door.OnStart()
    local state=_G.ApartmentStory
    assert(state.searchStep==1 and not state.hasKeys and locked)
    controller.OnUpdate(0.1)
    assert(velocity.x==0 and velocity.y==0 and velocity.z==0 and not jump)
    counter.OnInteract(player)
    keys.OnInteract(player)
    assert(state.searchStep==1 and not state.hasKeys)
    calls={}
    opening.OnUpdate(4.5)
    assert(calls[1][1]=='rect' and calls[#calls][2]=='Child: Mum, Teddy needs the blue plaster.')
    opening.OnUpdate(16.0)
    assert(locked and not state.openingComplete)
    opening.OnUpdate(3.8)
    assert(not locked and state.openingComplete)
    assert(player:GetCamera().isPrimary)
    sofa.OnInteract(player)
    assert(state.searchStep==1)
    counter.OnInteract(player)
    assert(state.searchStep==2)
    counter.OnInteract(player)
    sofa.OnInteract(player)
    assert(state.searchStep==2)
    door.OnInteract(player)
    assert(not state.leftApartment)
    coat.OnInteract(player)
    assert(state.searchStep==3)
    sofa.OnInteract(player)
    local response=state.message
    keys.OnUpdate(0.1)
    assert(not state.keysRevealed and state.message==response and keyEntity:GetTransform().position.y==-1000)
    keys.OnInteract(player)
    assert(not state.hasKeys)
    opening.OnUpdate(5.1)
    keys.OnUpdate(0.1)
    assert(state.keysRevealed and keyEntity:GetTransform().position.y==1.508)
    keys.OnInteract(player)
    assert(state.hasKeys and keyEntity:GetTransform().position.y==-1000)
    counter.OnInteract(player)
    assert(state.message=='I checked right here.')
    door.OnInteract(player)
    assert(state.leftApartment)
end
local function pressEscape()
    escapePressed=true
    opening.OnUpdate(0.016)
    escapePressed=false
end

opening.OnStart()
for _,text in ipairs({
    'Child: Mum, Teddy needs the blue plaster.',
    "Mother: Again? That's his third this week.",
    "Child: I'm practising.",
    'Mother: For what?',
    "Child: For when I'm a doctor.",
}) do
    calls={}
    pressEscape()
    assert(calls[#calls][2]==text and locked)
    opening.OnUpdate(0.1)
    assert(calls[#calls][2]==text and locked)
end
pressEscape()
assert(locked and not _G.ApartmentStory.openingComplete)
pressEscape()
assert(not locked and _G.ApartmentStory.openingComplete and fadeAlpha==0)
assert(player:GetCamera().isPrimary)
pressEscape()
assert(_G.ApartmentStory.searchStep==1 and not locked)

opening.OnStart()
opening.OnUpdate(5.8)
calls={}
pressEscape()
assert(calls[#calls][2]=="Mother: Again? That's his third this week.")
opening.OnStart()
opening.OnUpdate(21.5)
assert(fadeAlpha>0 and locked)
pressEscape()
assert(not locked and fadeAlpha==0 and _G.ApartmentStory.openingComplete)
opening.OnStart()
assert(locked and not _G.ApartmentStory.openingComplete)
print('PASS: subtitle timing/layering, control lock, ordered searches, delayed reveal, pickup, door, replay, Esc line/gap/wake skipping and fade cleanup')
