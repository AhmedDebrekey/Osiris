//
// Created by Debreky on 01/06/2026.
//

#ifndef OSIRIS_RHITYPES_H
#define OSIRIS_RHITYPES_H
#include <cstdint>


constexpr uint32_t INVALID_HANDLE_ID = UINT32_MAX;

template<typename Tag>
struct Handle {
    uint32_t id = INVALID_HANDLE_ID;
    bool IsValid() const { return id != INVALID_HANDLE_ID; }
};

struct BufferTag   {};
struct TextureTag  {};
struct PipelineTag {};
struct ShaderTag   {};
struct SamplerTag  {};
struct MaterialTag {};

using BufferHandle   = Handle<BufferTag>;
using TextureHandle  = Handle<TextureTag>;
using PipelineHandle = Handle<PipelineTag>;
using ShaderHandle   = Handle<ShaderTag>;
using SamplerHandle  = Handle<SamplerTag>;
using MaterialHandle = Handle<MaterialTag>;
#include <string>

enum class TextureFormat {
    RGBA8_UNORM,
    RGBA16_FLOAT,
    D32_FLOAT,
    BC7_UNORM,
};

enum class BufferUsage {
    Vertex,
    Index,
    Uniform,
    Storage,
    Transfer
};

struct BufferDesc {
    uint64_t    size  = 0;
    BufferUsage usage = BufferUsage::Vertex;
    bool        cpuVisible = false;
    const char* debugName  = nullptr;
};

struct TextureDesc {
    const void*     pixels = nullptr;
    uint64_t        dataSize  = 0;
    uint32_t        width     = 0;
    uint32_t        height    = 0;
    uint32_t        mipLevels = 1;
    TextureFormat   format    = TextureFormat::RGBA8_UNORM;
    const char*     debugName = nullptr;
};

struct ShaderDesc {
    const char* path       = nullptr;
    const char* entryPoint = "main";
};

struct MaterialDesc {
    TextureHandle albedo;
    TextureHandle normal;
    TextureHandle metallic;
    TextureHandle roughness;
    TextureHandle ao;
};


#endif //OSIRIS_RHITYPES_H