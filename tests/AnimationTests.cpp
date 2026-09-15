#define SDL_MAIN_HANDLED
#include "animation/Animation.h"
#include "animation/AnimationSerialization.h"
#include "assets/AnimationLoader.h"
#include "assets/MeshLoader.h"
#include "assets/SceneLoader.h"
#include "assets/TextureLoader.h"
#include "core/AssetManager.h"
#include "core/Log.h"
#include "platform/Input.h"
#include "renderer/Camera.h"
#include "scene/Scene.h"
#include "scripting/lua/LuaScripting.h"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <fastgltf/core.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace Osiris;
namespace {
    void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
    bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }

    // Records uploads and draws without creating a window, Vulkan instance, audio, or physics.
    class RecordingRHI final : public IRHI {
    public:
        std::vector<std::vector<char>> m_Buffers;
        std::vector<std::vector<glm::mat4>> m_Palettes;
        std::vector<uint32_t> m_Draws;
        std::vector<MaterialDesc> m_Materials;
        uint32_t m_Palette = INVALID_HANDLE_ID;
        uint32_t m_Textures = 0;
        bool m_Preview = false;
        float m_Exposure = 1;
        PostProcessSettings m_Post;
        DirectionalLight m_Light;
        ShadowSettings m_Shadow;
        bool Init() override { return true; }
        void Shutdown() override {}
        void BeginFrame() override {}
        void EndFrame() override {}
        void Present() override {}
        void BeginGPUTimestamp(const std::string&) override {}
        void EndGPUTimestamp(const std::string&) override {}
        std::vector<std::pair<std::string,float>> GetGPUTimings() const override { return {}; }
        void UploadBufferData(BufferHandle handle, const void* data, uint64_t size) override {
            Check(size == m_Buffers.at(handle.id).size(), "upload size");
            memcpy(m_Buffers.at(handle.id).data(), data, size);
        }
        void UploadDynamicBuffer(BufferHandle handle, const void* data, uint64_t size) override { UploadBufferData(handle,data,size); }
        void SetMeshData(const Mesh&) override {}
        void SetModelMatrix(const glm::mat4&) override {}
        void SetEmissive(const glm::vec3&,float) override {}
        void PrepareSkinning(const std::vector<std::vector<glm::mat4>>& palettes) override { m_Palettes = palettes; }
        void SetSkinPalette(uint32_t index) override { m_Palette = index; }
        void UpdateCamera(const glm::mat4&,const glm::mat4&,const glm::vec4&,const glm::vec3&) override {}
        void SetCameraBuffer(const glm::mat4&,const glm::mat4&,const glm::vec4&) override {}
        BufferHandle CreateBuffer(const BufferDesc& desc) override {
            m_Buffers.emplace_back(desc.size);
            return BufferHandle{static_cast<uint32_t>(m_Buffers.size()-1)};
        }
        TextureHandle CreateTexture(const TextureDesc& desc) override {
            Check(desc.pixels && desc.width == 2 && desc.height == 2 && desc.mipLevels == 2, "embedded/external texture decode");
            return TextureHandle{m_Textures++};
        }
        ShaderHandle CreateShader(const ShaderDesc&) override { return {}; }
        MaterialHandle CreateMaterial(const MaterialDesc& desc) override {
            m_Materials.push_back(desc); return MaterialHandle{static_cast<uint32_t>(m_Materials.size()-1)};
        }
        MaterialAlphaMode GetMaterialAlphaMode(MaterialHandle handle) const override { return m_Materials.at(handle.id).alphaMode; }
        void DestroyBuffer(BufferHandle) override {}
        void DestroyTexture(TextureHandle) override {}
        void DestroyShader(ShaderHandle) override {}
        void BindMaterial(MaterialHandle) override {}
        void BindPipeline(PipelineHandle) override {}
        void Draw(uint32_t) override {}
        void DrawIndexed(uint32_t) override { m_Draws.push_back(m_Palette); }
        void BeginShadowPass(uint32_t) override {}
        void EndShadowPass(uint32_t) override {}
        void DrawShadowIndexed(uint32_t) override { m_Draws.push_back(m_Palette); }
        void BeginForwardPass() override {}
        void BeginViewportForwardPass() override {}
        void ResizeViewport(uint32_t,uint32_t) override {}
        uint64_t GetViewportTextureID() const override { return 0; }
        uint64_t GetShadowCascadeTextureID(uint32_t) const override { return 0; }
        uint64_t GetSpotShadowTextureID(uint32_t) const override { return 0; }
        uint64_t GetEditorIconTextureID(EditorIcon) const override { return 0; }
        glm::uvec2 GetRenderExtent(bool) const override { return {1280,720}; }
        void UpdateSpotLights(const std::vector<SpotLightRenderData>&) override {}
        bool LoadEnvironmentMap(const float*,uint32_t,uint32_t) override { return false; }
        float& GetEnvironmentExposure() override { return m_Exposure; }
        void BeginSpotShadowPass(uint32_t) override {}
        void EndSpotShadowPass(uint32_t) override {}
        void InitImGui() override {}
        void ShutdownImGui() override {}
        void BeginImGuiFrame() override {}
        void RenderImGui(bool) override {}
        bool& GetPostProcessPreviewEnabled() override { return m_Preview; }
        uint64_t GetPostProcessPreviewTextureID() const override { return 0; }
        PostProcessSettings& GetPostProcessSettings() override { return m_Post; }
        DirectionalLight& GetDirectionalLight() override { return m_Light; }
        glm::mat4 GetLightViewMatrix(uint32_t) const override { return glm::mat4(1); }
        glm::mat4 GetLightProjMatrix(uint32_t) const override { return glm::mat4(1); }
        ShadowSettings& GetShadowSettings() override { return m_Shadow; }
        void Dispatch(uint32_t,uint32_t,uint32_t) override {}
        glm::mat4 GetLightSpaceMatrix(uint32_t) const override { return GetActiveLightSpaceMatrix(); }
        glm::mat4 GetActiveLightSpaceMatrix() const override { return glm::ortho(-10.f,10.f,-10.f,10.f,-10.f,10.f); }
    };

    std::shared_ptr<AnimationAsset> TestAsset() {
        auto asset = std::make_shared<AnimationAsset>();
        asset->bindPose.resize(2);
        asset->animatedNodes = {true, true};
        for (int i = 0; i < 3; i++) {
            AnimationClip clip;
            clip.name = std::vector<std::string>{"Idle","Walk","Run"}[i];
            clip.duration = 1;
            clip.channels.push_back({0, AnimationPath::Translation, AnimationInterpolation::Linear,
                {0,1}, {glm::vec4(i*20.f,0,0,0),glm::vec4(i*20.f+10,0,0,0)}});
            asset->clips.push_back(clip);
        }
        return asset;
    }

    void TestPlayback() {
        const auto asset = TestAsset();
        Animator a; a.SetAsset(asset);
        Check(a.Play("Idle",0,true), "play");
        a.Update(.25f);
        Check(Near(a.GetPose()[0].translation.x,2.5f), "linear sample");
        Check(a.Play("Idle",0,true) && Near(a.GetTime(),.25f), "same clip does not rewind");
        a.Play("Walk",.4f,true);
        Check(Near(a.GetPose()[0].translation.x,2.5f), "fade starts at current pose");
        a.Update(.1f);
        Check(Near(a.GetPose()[0].translation.x,7.875f), "both fade sources advance");
        a.Play("Run",.4f,true);
        Check(Near(a.GetPose()[0].translation.x,7.875f), "interrupted fade continuity");
        a.Pause(); const float time = a.GetTime(); a.Update(1);
        Check(a.GetTime() == time, "pause");
        a.Resume(); a.Update(.4f);
        Check(Near(a.GetPose()[0].translation.x,44), "fade completion");
        a.Play("Idle",0,false); a.Update(4);
        Check(a.IsFinished() && Near(a.GetPose()[0].translation.x,10), "one shot clamps");
        a.Seek(.5f); Check(Near(a.GetPose()[0].translation.x,5), "seek");
        a.Play("Idle",0,true); a.Seek(0); a.Update(2.25f);
        Check(Near(a.GetTime(),.25f), "loop wraps");
        Check(!a.Play("missing") && !a.SetSpeed(-1), "bad inputs rejected");
        Animator independent; independent.SetAsset(asset); independent.Play("Run",0,true); independent.Update(.75f);
        Check(Near(a.GetTime(),.25f), "independent instances");
        a.Stop(); Check(!a.IsPlaying() && Near(a.GetPose()[0].translation.x,0), "bind pose stop");

        auto modes = TestAsset();
        auto& channel = modes->clips[0].channels[0];
        channel.interpolation = AnimationInterpolation::Step;
        Animator step; step.SetAsset(modes); step.Play("Idle",0,false); step.Update(.9f);
        Check(Near(step.GetPose()[0].translation.x,0), "step before key");
        step.Update(.1f); Check(Near(step.GetPose()[0].translation.x,10), "step exact key");
        channel.interpolation = AnimationInterpolation::CubicSpline;
        channel.times = {0,2}; modes->clips[0].duration=2;
        channel.values = {glm::vec4(0),glm::vec4(0),glm::vec4(1,0,0,0),glm::vec4(1,0,0,0),glm::vec4(2,0,0,0),glm::vec4(0)};
        step.SetAsset(modes); step.Play("Idle",0,false); step.Update(1);
        Check(Near(step.GetPose()[0].translation.x,1), "cubic tangent interval scaling");
        modes->clips[0].channels.push_back({1, AnimationPath::Rotation, AnimationInterpolation::Linear,
            {0,2}, {glm::vec4(0,0,0,1),glm::vec4(0,0,0,-1)}});
        step.Seek(1);
        Check(Near(std::abs(step.GetPose()[1].rotation.w),1), "shortest quaternion path");
    }

    void TestGraphAndPersistence() {
        Animator a; a.SetAsset(TestAsset());
        auto& graph = a.GetGraph();
        Check(graph.AddState("Idle","Idle") && graph.AddState("Walk","Walk") && graph.AddState("Run","Run"), "graph states");
        Check(!graph.AddState("Idle","Walk"), "duplicate state");
        Check(graph.AddTransition("Idle","Walk","speed",AnimationComparison::Greater,.1f,.2f), "graph transition");
        Check(graph.AddTransition("Walk","Run","speed",AnimationComparison::Greater,3,.2f), "run transition");
        Check(!graph.AddTransition("missing","Idle","speed",AnimationComparison::Equal,0), "invalid transition");
        Check(a.StartGraph("Idle"), "graph start");
        a.SetFloat("speed",5); a.Update(.01f);
        Check(a.GetCurrentState()=="Walk", "at most one transition per update");
        a.Update(.01f); Check(a.GetCurrentState()=="Run", "graph run");
        a.SetBool("grounded",true); a.SetSpeed(.7f); a.Pause();
        const auto json = SaveAnimator(a);
        Animator restored; restored.SetAsset(a.GetAsset()); std::string error;
        Check(LoadAnimator(restored,json,error), "graph deserialize");
        Check(SaveAnimator(restored)==json && restored.GetTime()==0, "graph round trip without transient time");
        auto bad = json; bad["state"]="missing";
        Check(!LoadAnimator(restored,bad,error) && SaveAnimator(restored)==json, "invalid load is atomic");
        bad = json; bad["states"] = nlohmann::json::object();
        Check(!LoadAnimator(restored,bad,error), "invalid graph container");
        bad = json; bad["state"]=""; bad["states"][0]["clip"]="missing";
        Check(!LoadAnimator(restored,bad,error), "inactive graph clips are validated");
    }

    void TestScene(const std::filesystem::path& root) {
        RecordingRHI rhi;
        AssetManager::SetAssetRoots((root / "games/horror/assets").generic_string(), (root / "assets").generic_string());
        Scene scene;
        Entity left = scene.SpawnModel("Left","models/animation_test/test_character.glb",&rhi);
        Entity right = scene.SpawnModel("Right","models/animation_test/test_character.gltf",&rhi);
        Check(left.IsValid() && right.IsValid() && left.HasComponent<AnimatorComponent>(), "GLB/glTF spawn");
        Check(rhi.m_Textures==2, "both external and embedded images decode");
        const size_t bufferCount = rhi.m_Buffers.size();
        Entity cached = scene.SpawnModel("Cached","models/animation_test/test_character.glb",&rhi);
        Check(rhi.m_Buffers.size()==bufferCount, "model cache preserves animated assets");
        scene.DestroyEntity(cached,nullptr,nullptr,nullptr);
        left.GetComponent<TransformComponent>().position.x=-1.1f;
        right.GetComponent<TransformComponent>().position.x=1.1f;
        auto& player = left.GetComponent<AnimatorComponent>().player;
        auto& second = right.GetComponent<AnimatorComponent>().player;
        Check(player.Play("Walk",0,true) && second.Play("Idle",0,true), "clips imported by name");
        scene.CapturePlaySnapshot();
        scene.UpdateAnimations(.25f,true); scene.PrepareSkinning(&rhi);
        Check(rhi.m_Palettes.size()==2 && rhi.m_Palettes[0].size()==6, "per-instance palettes");
        Check(!Near(rhi.m_Palettes[0][1][1][1],rhi.m_Palettes[1][1][1][1]), "independent clip poses");
        Check(Near(left.GetComponent<TransformComponent>().position.x,-1.1f), "animation never moves controller root");
        Camera camera({0,1,5},{0,0,-1});
        scene.Render(&rhi,camera);
        Check(rhi.m_Draws.size()==2 && rhi.m_Draws[0]!=rhi.m_Draws[1], "forward selects independent palettes");
        const auto forward = rhi.m_Draws; rhi.m_Draws.clear(); scene.RenderShadows(&rhi);
        Check(rhi.m_Draws==forward, "shadow uses same palette selection");
        const auto mesh = scene.FindEntityByName("Left_Body_1");
        Check(mesh.IsValid(), "stable imported node names");
        const auto& bounds = scene.GetMeshBounds(mesh);
        Check(bounds.min.x < bounds.max.x && bounds.max.y > 1.6f, "deformed bounds");
        scene.RestorePlaySnapshot(nullptr,nullptr,nullptr);
        Check(player.GetTime()==0 && player.GetCurrentClip()=="Walk", "F5 animator snapshot reset");
        scene.UpdateAnimations(1,false);
        Check(player.GetTime()==0, "edit does not advance without preview");
        left.GetComponent<AnimatorComponent>().previewInEditor=true;
        scene.UpdateAnimations(.25f,false); Check(Near(player.GetTime(),.25f), "editor preview");

        Input input;
        LuaScripting scripting;
        Check(scripting.Init(&rhi,nullptr,nullptr,&input,nullptr), "Lua init without engine");
        const auto instance = scripting.CreateInstance(left,(root / "tests/animation_binding_test.lua").generic_string());
        Check(instance.IsValid(), "Lua test script");
        scripting.Update(.01f); scene.UpdateAnimations(.01f,true);
        Check(player.GetFloat("testPassed")==1 && player.GetCurrentState()=="Run", "Lua graph API");
        scripting.DestroyInstance(instance); scripting.Shutdown();

        const auto savePath = root / ".cache/animation_scene_roundtrip.json";
        SceneLoader::Save(savePath.generic_string(),scene);
        Scene loaded; SceneLoader::Load(savePath.generic_string(),loaded,&rhi,nullptr);
        auto loadedLeft = loaded.FindEntityByName("Left");
        Check(loadedLeft.IsValid() && loadedLeft.GetComponent<AnimatorComponent>().player.GetCurrentState()=="Run", "scene persistence");
        Check(!loadedLeft.GetComponent<AnimatorComponent>().previewInEditor, "preview not persisted");
        loaded.PrepareSkinning(&rhi); Check(rhi.m_Palettes.size()==2, "skin bindings survive reload");
        loadedLeft.RemoveComponent<AnimatorComponent>();
        loaded.CapturePlaySnapshot();
        loaded.AddAnimator(loadedLeft);
        loaded.RestorePlaySnapshot(nullptr,nullptr,nullptr);
        Check(!loadedLeft.HasComponent<AnimatorComponent>(), "runtime-added animator is removed on F5");
        SceneLoader::Save(savePath.generic_string(),loaded);
        Scene withoutAnimator; SceneLoader::Load(savePath.generic_string(),withoutAnimator,&rhi,nullptr);
        Check(!withoutAnimator.FindEntityByName("Left").HasComponent<AnimatorComponent>(), "removed animator persists");
        withoutAnimator.Clear(nullptr,nullptr,nullptr);
        scene.Clear(nullptr,nullptr,nullptr); loaded.Clear(nullptr,nullptr,nullptr);
        MeshLoader::ClearCache(&rhi); TextureLoader::ClearCache(&rhi);
    }
}

int main(int argc,char** argv) {
    try {
        Log logger; logger.Initialize();
        const auto root = argc>1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
        TestPlayback(); TestGraphAndPersistence(); TestScene(root);
        std::cout << "PASS: animation sampling, looping, interrupted fades, graphs, serialization, GLB/glTF, embedded textures, caching, scene palettes/bounds, Lua bindings, F5 reset. No engine or GPU was started.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
