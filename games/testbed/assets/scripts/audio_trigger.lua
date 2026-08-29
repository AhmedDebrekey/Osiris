-- Press E to toggle this entity's own looping emitter on/off.

function OnStart()
    print("audio_trigger: press E to toggle " .. self:GetTag().name)
end

function OnUpdate(dt)
    if input:IsKeyPressed(Key.E) then
        local audioSrc = self:GetAudioSource()
        if audio:IsSourcePlaying(audioSrc.sourceHandle) then
            audio:StopSource(audioSrc.sourceHandle)
        else
            audio:PlaySource(audioSrc.sourceHandle)
        end
    end
end

function OnFixedUpdate(fixedDt)
end
