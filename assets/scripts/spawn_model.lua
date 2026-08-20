-- Showcases scene:SpawnModel, spawning a textured box and positioning every returned part.

function OnStart()
    local parts = scene:SpawnModel("ShowcaseBox", "models/BoxTexturedGLTF/BoxTextured.gltf")
    print("spawn_model: SpawnModel returned " .. #parts .. " part(s)")

    for i = 1, #parts do
        local part = parts[i]
        local transform = part:GetTransform()
        transform.position = vec3.new(-2.0, 1.0, 2.5)
        print("spawn_model: spawned " .. part:GetTag().name)
    end
end

function OnUpdate(dt)
end

function OnFixedUpdate(fixedDt)
end
