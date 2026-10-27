// SpyroShaderRecomp entry point (fork of XenosRecomp's main.cpp, WORK_LOG 103).
//
//   SpyroShaderRecomp <shader file> <output .hlsl> <shader_common.h>
//       one container -> HLSL text (unchanged from XenosRecomp)
//   SpyroShaderRecomp <directory> <output cache .bin> <shader_common.h>
//       every container found in the files under <directory> -> DXIL + SPIR-V, written to one
//       binary cache that the plume renderer loads at start-up (plume_shader_cache.cpp).
//
// Cache file (little-endian):
//   u32 magic 'SPSC' (0x43535053), u32 version (1), u32 entry count, u32 reserved
//   entries[count] = { u64 hash, u32 dxilOffset, u32 dxilSize, u32 spirvOffset, u32 spirvSize,
//                      u32 flags (bit 0 = pixel shader), u32 specConstantsMask }   (32 bytes)
//   then the DXIL and SPIR-V blobs (offsets are from the start of the file).
// hash = 64-bit FNV-1a over the container's first virtualSize + physicalSize bytes - the same
// hash the game-side capture (plume_hooks.cpp DumpShader) and the renderer compute.

#include "shader.h"
#include "shader_recompiler.h"
#include "dxc_compiler.h"

static std::unique_ptr<uint8_t[]> readAllBytes(const char* filePath, size_t& fileSize)
{
    FILE* file = fopen(filePath, "rb");
    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    auto data = std::make_unique<uint8_t[]>(fileSize);
    fread(data.get(), 1, fileSize, file);
    fclose(file);
    return data;
}

static void writeAllBytes(const char* filePath, const void* data, size_t dataSize)
{
    FILE* file = fopen(filePath, "wb");
    if (file == nullptr)
    {
        fmt::println("could not write {}", filePath);
        return;
    }
    fwrite(data, 1, dataSize, file);
    fclose(file);
}

static uint64_t fnv1a64(const uint8_t* data, size_t size)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; i++)
        h = (h ^ data[i]) * 1099511628211ull;
    return h;
}

struct RecompiledShader
{
    const uint8_t* data = nullptr;
    std::string source;  // file the container came from (for error reports)
    IDxcBlob* dxil = nullptr;
    IDxcBlob* spirv = nullptr;
    bool isPixelShader = false;
    uint32_t specConstantsMask = 0;
};

#pragma pack(push, 1)
struct CacheHeader
{
    uint32_t magic = 0x43535053;
    uint32_t version = 1;
    uint32_t count = 0;
    uint32_t reserved = 0;
};

struct CacheEntry
{
    uint64_t hash;
    uint32_t dxilOffset, dxilSize;
    uint32_t spirvOffset, spirvSize;
    uint32_t flags;
    uint32_t specConstantsMask;
};
#pragma pack(pop)
static_assert(sizeof(CacheEntry) == 32);

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        printf("Usage: SpyroShaderRecomp [input file or directory] [output .hlsl or cache .bin] [shader_common.h]\n");
        return 0;
    }

    const char* input = argv[1];
    const char* output = argv[2];

    size_t includeSize = 0;
    auto includeData = readAllBytes(argv[3], includeSize);
    std::string_view include(reinterpret_cast<const char*>(includeData.get()), includeSize);

    if (!std::filesystem::is_directory(input))
    {
        ShaderRecompiler recompiler;
        size_t fileSize;
        recompiler.recompile(readAllBytes(input, fileSize).get(), include);
        writeAllBytes(output, recompiler.out.data(), recompiler.out.size());
        return 0;
    }

    std::vector<std::unique_ptr<uint8_t[]>> files;
    std::map<uint64_t, RecompiledShader> shaders;

    for (auto& file : std::filesystem::recursive_directory_iterator(input))
    {
        if (std::filesystem::is_directory(file))
            continue;

        size_t fileSize = 0;
        auto fileData = readAllBytes(file.path().string().c_str(), fileSize);
        bool foundAny = false;

        for (size_t i = 0; fileSize > sizeof(ShaderContainer) && i < fileSize - sizeof(ShaderContainer) - 1;)
        {
            auto shaderContainer = reinterpret_cast<const ShaderContainer*>(fileData.get() + i);
            size_t dataSize = shaderContainer->virtualSize + shaderContainer->physicalSize;

            if ((shaderContainer->flags & 0xFFFFFF00) == 0x102A1100 &&
                dataSize <= (fileSize - i) &&
                shaderContainer->field1C == 0 &&
                shaderContainer->field20 == 0)
            {
                uint64_t hash = fnv1a64(fileData.get() + i, dataSize);
                auto shader = shaders.try_emplace(hash);
                if (shader.second)
                {
                    shader.first->second.data = fileData.get() + i;
                    shader.first->second.source = file.path().filename().string();
                    foundAny = true;
                }
                i += dataSize;
            }
            else
            {
                i += sizeof(uint32_t);
            }
        }

        if (foundAny)
            files.emplace_back(std::move(fileData));
    }

    std::atomic<uint32_t> progress = 0;
    std::mutex errorMutex;

    std::for_each(std::execution::par_unseq, shaders.begin(), shaders.end(), [&](auto& hashShaderPair)
        {
            auto& shader = hashShaderPair.second;

            thread_local ShaderRecompiler recompiler;
            recompiler = {};
            recompiler.recompile(shader.data, include);
            shader.isPixelShader = recompiler.isPixelShader;
            shader.specConstantsMask = recompiler.specConstantsMask;

            // [Spyro] env SPYRO_DUMPHLSL=<folder>: also save each shader's HLSL as <hash>.hlsl
            // (debugging the plume renderer, WORK_LOG 106).
            if (const char* hlslDir = std::getenv("SPYRO_DUMPHLSL"))
            {
                std::string path = fmt::format("{}/{:016X}.hlsl", hlslDir, hashShaderPair.first);
                writeAllBytes(path.c_str(), recompiler.out.data(), recompiler.out.size());
            }

            thread_local DxcCompiler dxcCompiler;
#ifdef XENOS_RECOMP_DXIL
            shader.dxil = dxcCompiler.compile(recompiler.out, recompiler.isPixelShader, false, false);
#endif
            shader.spirv = dxcCompiler.compile(recompiler.out, recompiler.isPixelShader, false, true);

            if (shader.dxil == nullptr || shader.spirv == nullptr)
            {
                std::lock_guard lock(errorMutex);
                fmt::println("FAILED: {} ({:016X}): dxil {} spirv {}", shader.source, hashShaderPair.first,
                    shader.dxil != nullptr ? "ok" : "failed", shader.spirv != nullptr ? "ok" : "failed");
            }

            size_t currentProgress = ++progress;
            if ((currentProgress % 100) == 0)
                fmt::println("Recompiling shaders... {}/{}", currentProgress, shaders.size());
        });

    CacheHeader header;
    std::vector<CacheEntry> entries;
    std::vector<uint8_t> blobs;
    for (auto& [hash, shader] : shaders)
    {
        if (shader.dxil == nullptr || shader.spirv == nullptr)
            continue;
        CacheEntry e{};
        e.hash = hash;
        e.flags = shader.isPixelShader ? 1 : 0;
        e.specConstantsMask = shader.specConstantsMask;
        e.dxilOffset = uint32_t(blobs.size());
        e.dxilSize = uint32_t(shader.dxil->GetBufferSize());
        auto dxil = reinterpret_cast<const uint8_t*>(shader.dxil->GetBufferPointer());
        blobs.insert(blobs.end(), dxil, dxil + e.dxilSize);
        e.spirvOffset = uint32_t(blobs.size());
        e.spirvSize = uint32_t(shader.spirv->GetBufferSize());
        auto spirv = reinterpret_cast<const uint8_t*>(shader.spirv->GetBufferPointer());
        blobs.insert(blobs.end(), spirv, spirv + e.spirvSize);
        entries.push_back(e);
    }

    header.count = uint32_t(entries.size());
    uint32_t blobBase = uint32_t(sizeof(CacheHeader) + entries.size() * sizeof(CacheEntry));
    for (auto& e : entries)
    {
        e.dxilOffset += blobBase;
        e.spirvOffset += blobBase;
    }

    std::vector<uint8_t> out(sizeof(CacheHeader) + entries.size() * sizeof(CacheEntry));
    memcpy(out.data(), &header, sizeof(header));
    if (!entries.empty())
        memcpy(out.data() + sizeof(header), entries.data(), entries.size() * sizeof(CacheEntry));
    out.insert(out.end(), blobs.begin(), blobs.end());
    writeAllBytes(output, out.data(), out.size());

    fmt::println("Shader cache: {} of {} shaders, {} KB -> {}", entries.size(), shaders.size(), out.size() / 1024, output);
    return entries.size() == shaders.size() ? 0 : 1;
}
