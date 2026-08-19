-- Cross-entity manipulation test: this script lives on its own "ScriptController" entity
-- (no mesh, just Tag+Transform) and reaches into a *different* entity's SpotLightComponent
-- every frame via scene:FindEntityByName — proves a script can grab a handle to another
-- entity and manipulate its components, not just its own.

local t = 0

function OnStart()
    print("pulse_light: started")
end

function OnUpdate(dt)
    t = t + dt

    local light = scene:FindEntityByName("SpotLight_Accent")
    if light ~= nil and light:IsValid() and light:HasSpotLight() then
        local spotLight = light:GetSpotLight()
        spotLight.intensity = 10.0 + math.sin(t * 2.0) * 8.0
    end
end

function OnFixedUpdate(fixedDt)
end
