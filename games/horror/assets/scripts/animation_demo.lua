function OnStart()
    local animator = self:GetAnimator()
    local graph = animator:GetGraph()
    graph:Clear()
    assert(graph:AddState("Idle", "Idle"))
    assert(graph:AddState("Walk", "Walk"))
    assert(graph:AddState("Run", "Run"))
    assert(graph:AddTransition("Idle", "Run", "speed", AnimationComparison.Greater, 3.0, 0.2))
    assert(graph:AddTransition("Idle", "Walk", "speed", AnimationComparison.Greater, 0.1, 0.2))
    assert(graph:AddTransition("Walk", "Idle", "speed", AnimationComparison.LessEqual, 0.1, 0.2))
    assert(graph:AddTransition("Walk", "Run", "speed", AnimationComparison.Greater, 3.0, 0.2))
    assert(graph:AddTransition("Run", "Idle", "speed", AnimationComparison.LessEqual, 0.1, 0.2))
    assert(graph:AddTransition("Run", "Walk", "speed", AnimationComparison.LessEqual, 3.0, 0.2))
    assert(animator:StartGraph("Idle"))
end

function OnUpdate(dt)
    -- Fetched fresh each frame rather than cached, since adding/removing an Animator on any
    -- entity can relocate every entity's AnimatorComponent storage.
    local animator = self:GetAnimator()
    -- This stationary rig isolates animation from movement. A real controller supplies its speed.
    local speed = 0.0
    if input:IsKeyHeld(Key.W) then speed = input:IsKeyHeld(Key.LeftShift) and 5.0 or 2.0 end
    animator:SetFloat("speed", speed)
    ui.Text({x=0.5, y=0.06, anchor="topcenter", text="Hold W: Walk | Shift + W: Run | Release: Idle | F5: Edit", size=20})
    ui.Text({x=0.5, y=0.11, anchor="topcenter", text="Left: Lua graph (" .. animator:GetCurrentState() .. ") | Right: independent looping Walk", size=18})
end
