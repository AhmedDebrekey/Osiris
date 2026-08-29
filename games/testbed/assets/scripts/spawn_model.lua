-- Showcases scene:SpawnModel, spawning and positioning one model root.

function OnStart()
    local model = scene:SpawnModel("ShowcaseBox", "models/BoxTexturedGLTF/BoxTextured.gltf")
    if model:IsValid() then
        local transform = model:GetTransform()
        transform.position = vec3.new(-2.0, 1.0, 2.5)
        print("spawn_model: spawned " .. model:GetTag().name)
    end
end

function OnUpdate(dt)
end

function OnFixedUpdate(fixedDt)
end
