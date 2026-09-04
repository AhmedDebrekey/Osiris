//
// Created by Debreky on 01/06/2026.
//

#ifndef OSIRIS_RHI_H
#define OSIRIS_RHI_H

#include "RHITypes.h"
#include "renderer/MeshType.h"
#include "renderer/Light.h"
#include <glm/glm.hpp>
#include <string>
#include <utility>
#include <vector>

namespace Osiris {
    class IRHI {
        public:
        virtual ~IRHI() = default;

        virtual bool Init()     = 0;
        virtual void Shutdown() = 0;

        virtual void BeginFrame()   = 0;
        virtual void EndFrame()     = 0;
        virtual void Present()      = 0;

        virtual void BeginGPUTimestamp(const std::string& name) = 0;
        virtual void EndGPUTimestamp(const std::string& name) = 0;
        virtual std::vector<std::pair<std::string, float>> GetGPUTimings() const = 0;

        virtual void UploadBufferData(BufferHandle handle, const void* data, uint64_t size) = 0;
        virtual void UploadDynamicBuffer(BufferHandle handle, const void* data, uint64_t size) = 0;
        virtual void SetMeshData(const Mesh& mesh) = 0;
        virtual void SetModelMatrix(const glm::mat4& model) = 0;
        virtual void SetEmissive(const glm::vec3& color, float intensity) = 0;

        virtual void UpdateCamera(const glm::mat4& view, const glm::mat4& projection, const glm::vec4& position, const glm::vec3& front) = 0;
        virtual void SetCameraBuffer(const glm::mat4& view, const glm::mat4& projection, const glm::vec4& position) = 0;

        virtual BufferHandle    CreateBuffer(const BufferDesc&)          = 0;
        virtual TextureHandle   CreateTexture(const TextureDesc&)        = 0;
        virtual ShaderHandle    CreateShader(const ShaderDesc&)          = 0;
        virtual MaterialHandle  CreateMaterial(const MaterialDesc& desc) = 0;
        virtual MaterialAlphaMode GetMaterialAlphaMode(MaterialHandle handle) const = 0;

        virtual void DestroyBuffer (BufferHandle)  = 0;
        virtual void DestroyTexture(TextureHandle) = 0;
        virtual void DestroyShader (ShaderHandle)  = 0;

        virtual void BindMaterial       (MaterialHandle handle)    = 0;
        virtual void BindPipeline       (PipelineHandle pipeline)  = 0;
        virtual void Draw               (uint32_t vertexCount)     = 0;
        virtual void DrawIndexed        (uint32_t indexCount)      = 0;

        virtual void BeginShadowPass    (uint32_t cascadeIndex) = 0;
        virtual void EndShadowPass      (uint32_t cascadeIndex) = 0;
        virtual void DrawShadowIndexed  (uint32_t indexCount)   = 0;
        virtual void BeginForwardPass   ()                      = 0;
        virtual void BeginViewportForwardPass()                 = 0;
        virtual void ResizeViewport(uint32_t width, uint32_t height) = 0;
        virtual uint64_t GetViewportTextureID() const            = 0;
        virtual uint64_t GetShadowCascadeTextureID(uint32_t cascade) const = 0;
        virtual uint64_t GetSpotShadowTextureID(uint32_t index) const = 0;
        virtual uint64_t GetEditorIconTextureID(EditorIcon icon) const = 0;
        virtual glm::uvec2 GetRenderExtent(bool viewport) const = 0;


        virtual void UpdateSpotLights(const std::vector<SpotLightRenderData>& lights) = 0;

        // Takes raw RGBA32F equirectangular pixel data (the RHI doesn't know or
        // care about HDR file formats) and builds the environment cubemap used
        // for IBL. One-shot setup call, not part of the per-frame loop.
        virtual bool LoadEnvironmentMap(const float* pixels, uint32_t width, uint32_t height) = 0;
        virtual float& GetEnvironmentExposure() = 0;

        virtual void BeginSpotShadowPass(uint32_t index) = 0;
        virtual void EndSpotShadowPass(uint32_t index)   = 0;

        virtual void InitImGui()        = 0;
        virtual void ShutdownImGui()    = 0;
        virtual void BeginImGuiFrame()  = 0;
        virtual void RenderImGui(bool separatePass) = 0;

        // Post-processing (vignette/etc.) always applies in Play. In Edit mode it never touches
        // the normal viewport, this toggle instead generates a separate preview image, shown via
        // GetPostProcessPreviewTextureID(), the same "swap what the debug view shows" mechanism
        // the shadow cascade debug view already uses.
        virtual bool& GetPostProcessPreviewEnabled() = 0;
        virtual uint64_t GetPostProcessPreviewTextureID() const = 0;
        virtual PostProcessSettings& GetPostProcessSettings() = 0;

        virtual DirectionalLight& GetDirectionalLight() = 0;
        virtual glm::mat4 GetLightViewMatrix(uint32_t cascade) const = 0;
        virtual glm::mat4 GetLightProjMatrix(uint32_t cascade) const = 0;
        virtual ShadowSettings& GetShadowSettings() = 0;

        virtual void Dispatch (uint32_t x, uint32_t y, uint32_t z) = 0;

        // TEMP
        virtual glm::mat4 GetLightSpaceMatrix(uint32_t cascadeIndex) const = 0;

    };
}
#endif //OSIRIS_RHI_H
