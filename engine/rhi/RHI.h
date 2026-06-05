//
// Created by Debreky on 01/06/2026.
//

#ifndef OSIRIS_RHI_H
#define OSIRIS_RHI_H

#include "RHITypes.h"

namespace Osiris {
    class IRHI {
        public:
        virtual ~IRHI() = default;

        virtual bool Init()     = 0;
        virtual void Shutdown() = 0;

        virtual void BeginFrame()   = 0;
        virtual void EndFrame()     = 0;
        virtual void Present()      = 0;

        virtual void UploadBufferData(BufferHandle handle, const void* data, uint64_t size) = 0;
        virtual void SetVertexBuffer(const BufferHandle handle) = 0;

        virtual BufferHandle    CreateBuffer(const BufferDesc&)     = 0;
        virtual TextureHandle   CreateTexture(const TextureDesc&)   = 0;
        virtual ShaderHandle    CreateShader(const ShaderDesc&)     = 0;

        virtual void DestroyBuffer (BufferHandle)  = 0;
        virtual void DestroyTexture(TextureHandle) = 0;
        virtual void DestroyShader (ShaderHandle)  = 0;

        virtual void BindPipeline       (PipelineHandle pipeline)  = 0;
        virtual void Draw               (uint32_t vertexCount)     = 0;
        virtual void DrawIndexed        (uint32_t indexCount)      = 0;
        virtual void Dispatch           (uint32_t x, uint32_t y, uint32_t z) = 0;

    };
}
#endif //OSIRIS_RHI_H