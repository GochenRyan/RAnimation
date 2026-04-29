// © 2021 NVIDIA Corporation

#include <array>
#include <cassert>
#include <filesystem>
#include <functional>

#include <detex.h>
#include <stb_image.h>

#include <RHIWrap/Utils.h>

//========================================================================================================================
// TEXTURE
//========================================================================================================================

inline detexTexture** ToTexture(utils::Mip* mips) {
    return (detexTexture**)mips;
}

inline detexTexture* ToMip(utils::Mip mip) {
    return (detexTexture*)mip;
}

utils::Texture::~Texture() {
    detexFreeTexture(ToTexture(mips), mipNum);
}

void utils::Texture::GetSubresource(nri::TextureSubresourceUploadDesc& subresource, uint32_t mipIndex, uint32_t arrayIndex) const {
    // TODO: 3D images are not supported, "subresource.slices" needs to be allocated to store pointers to all slices of the current mipmap
    assert(GetDepth() == 1);
    (void)(arrayIndex); // TODO: unused

    detexTexture* mip = ToMip(mips[mipIndex]);

    int rowPitch, slicePitch;
    detexComputePitch(mip->format, mip->width, mip->height, &rowPitch, &slicePitch);

    subresource.slices = mip->data;
    subresource.sliceNum = 1;
    subresource.rowPitch = (uint32_t)rowPitch;
    subresource.slicePitch = (uint32_t)slicePitch;
}

bool utils::Texture::IsBlockCompressed() const {
    return detexFormatIsCompressed(ToMip(mips[0])->format);
}

const char* utils::GetFileName(const std::string& path) {
    const size_t slashPos = path.find_last_of("\\/");
    if (slashPos != std::string::npos)
        return path.c_str() + slashPos + 1;

    return "";
}

bool utils::LoadFile(const std::string& path, std::vector<uint8_t>& data)
{
    FILE* file = fopen(path.c_str(), "rb");

    if (file == nullptr)
    {
        printf("ERROR: File '%s' is not found!\n", path.c_str());
        data.clear();
        return false;
    }

    printf("Loading file '%s'...\n", GetFileName(path));

    fseek(file, 0, SEEK_END);
    const size_t size = ftell(file); // 32-bit size
    fseek(file, 0, SEEK_SET);

    data.resize(size);
    const size_t readSize = fread(&data[0], size, 1, file);
    fclose(file);

    return !data.empty() && readSize == 1;
}

inline const char* GetShaderExt(nri::GraphicsAPI graphicsAPI)
{
    if (graphicsAPI == nri::GraphicsAPI::D3D11)
        return ".dxbc";
    else if (graphicsAPI == nri::GraphicsAPI::D3D12)
        return ".dxil";

    return ".spirv";
}

struct Shader
{
    const char* ext;
    nri::StageBits stage;
};

constexpr std::array<Shader, 13> gShaderExts = {{
        {"", nri::StageBits::NONE},
        {".vs.", nri::StageBits::VERTEX_SHADER},
        {".tcs.", nri::StageBits::TESS_CONTROL_SHADER},
        {".tes.", nri::StageBits::TESS_EVALUATION_SHADER},
        {".gs.", nri::StageBits::GEOMETRY_SHADER},
        {".fs.", nri::StageBits::FRAGMENT_SHADER},
        {".cs.", nri::StageBits::COMPUTE_SHADER},
        {".rgen.", nri::StageBits::RAYGEN_SHADER},
        {".rmiss.", nri::StageBits::MISS_SHADER},
        {"<noimpl>", nri::StageBits::INTERSECTION_SHADER},
        {".rchit.", nri::StageBits::CLOSEST_HIT_SHADER},
        {".rahit.", nri::StageBits::ANY_HIT_SHADER},
        {"<noimpl>", nri::StageBits::CALLABLE_SHADER},
}};

nri::ShaderDesc utils::LoadShader(nri::GraphicsAPI graphicsAPI,
                                  const std::string& shaderName,
                                  ShaderCodeStorage& storage,
                                  const char* entryPointName)
{
    const char* ext = GetShaderExt(graphicsAPI);
    std::string path = shaderName + ext;
    nri::ShaderDesc shaderDesc = {};

    size_t i = 1;
    for (; i < gShaderExts.size(); i++)
    {
        if (path.rfind(gShaderExts[i].ext) != std::string::npos)
        {
            storage.push_back(std::vector<uint8_t>());
            std::vector<uint8_t>& code = storage.back();

            if (LoadFile(path, code))
            {
                shaderDesc.stage = gShaderExts[i].stage;
                shaderDesc.bytecode = code.data();
                shaderDesc.size = code.size();
                shaderDesc.entryPointName = entryPointName;
            }

            break;
        }
    }

    if (i == gShaderExts.size())
    {
        printf("ERROR: Shader '%s' has invalid shader extension!\n", shaderName.c_str());

        exit(1);
    }

    return shaderDesc;
}

static struct FormatMapping
{
    uint32_t detexFormat;
    nri::Format nriFormat;
} nriFormatTable[] = {
        // Uncompressed formats.
        {DETEX_PIXEL_FORMAT_RGB8, nri::Format::UNKNOWN},
        {DETEX_PIXEL_FORMAT_RGBA8, nri::Format::RGBA8_UNORM},
        {DETEX_PIXEL_FORMAT_R8, nri::Format::R8_UNORM},
        {DETEX_PIXEL_FORMAT_SIGNED_R8, nri::Format::R8_SNORM},
        {DETEX_PIXEL_FORMAT_RG8, nri::Format::RG8_UNORM},
        {DETEX_PIXEL_FORMAT_SIGNED_RG8, nri::Format::RG8_SNORM},
        {DETEX_PIXEL_FORMAT_R16, nri::Format::R16_UNORM},
        {DETEX_PIXEL_FORMAT_SIGNED_R16, nri::Format::R16_SNORM},
        {DETEX_PIXEL_FORMAT_RG16, nri::Format::RG16_UNORM},
        {DETEX_PIXEL_FORMAT_SIGNED_RG16, nri::Format::RG16_SNORM},
        {DETEX_PIXEL_FORMAT_RGB16, nri::Format::UNKNOWN},
        {DETEX_PIXEL_FORMAT_RGBA16, nri::Format::RGBA16_UNORM},
        {DETEX_PIXEL_FORMAT_FLOAT_R16, nri::Format::R16_SFLOAT},
        {DETEX_PIXEL_FORMAT_FLOAT_RG16, nri::Format::RG16_SFLOAT},
        {DETEX_PIXEL_FORMAT_FLOAT_RGB16, nri::Format::UNKNOWN},
        {DETEX_PIXEL_FORMAT_FLOAT_RGBA16, nri::Format::RGBA16_SFLOAT},
        {DETEX_PIXEL_FORMAT_FLOAT_R32, nri::Format::R32_SFLOAT},
        {DETEX_PIXEL_FORMAT_FLOAT_RG32, nri::Format::RG32_SFLOAT},
        {DETEX_PIXEL_FORMAT_FLOAT_RGB32, nri::Format::RGB32_SFLOAT},
        {DETEX_PIXEL_FORMAT_FLOAT_RGBA32, nri::Format::RGBA32_SFLOAT},
        {DETEX_PIXEL_FORMAT_A8, nri::Format::UNKNOWN},
        // Compressed formats.
        {DETEX_TEXTURE_FORMAT_BC1, nri::Format::BC1_RGBA_UNORM},
        {DETEX_TEXTURE_FORMAT_BC1A, nri::Format::UNKNOWN},
        {DETEX_TEXTURE_FORMAT_BC2, nri::Format::BC2_RGBA_UNORM},
        {DETEX_TEXTURE_FORMAT_BC3, nri::Format::BC3_RGBA_UNORM},
        {DETEX_TEXTURE_FORMAT_RGTC1, nri::Format::BC4_R_UNORM},
        {DETEX_TEXTURE_FORMAT_SIGNED_RGTC1, nri::Format::BC4_R_SNORM},
        {DETEX_TEXTURE_FORMAT_RGTC2, nri::Format::BC5_RG_UNORM},
        {DETEX_TEXTURE_FORMAT_SIGNED_RGTC2, nri::Format::BC5_RG_SNORM},
        {DETEX_TEXTURE_FORMAT_BPTC_FLOAT, nri::Format::BC6H_RGB_UFLOAT},
        {DETEX_TEXTURE_FORMAT_BPTC_SIGNED_FLOAT, nri::Format::BC6H_RGB_SFLOAT},
        {DETEX_TEXTURE_FORMAT_BPTC, nri::Format::BC7_RGBA_UNORM},
        {DETEX_TEXTURE_FORMAT_ETC1, nri::Format::UNKNOWN},
        {DETEX_TEXTURE_FORMAT_ETC2, nri::Format::UNKNOWN},
        {DETEX_TEXTURE_FORMAT_ETC2_PUNCHTHROUGH, nri::Format::UNKNOWN},
        {DETEX_TEXTURE_FORMAT_ETC2_EAC, nri::Format::UNKNOWN},
        {DETEX_TEXTURE_FORMAT_EAC_R11, nri::Format::UNKNOWN},
        {DETEX_TEXTURE_FORMAT_EAC_SIGNED_R11, nri::Format::UNKNOWN},
        {DETEX_TEXTURE_FORMAT_EAC_RG11, nri::Format::UNKNOWN},
        {DETEX_TEXTURE_FORMAT_EAC_SIGNED_RG11, nri::Format::UNKNOWN},
};

static nri::Format GetFormatNRI(uint32_t detexFormat)
{
    for (auto& entry : nriFormatTable)
    {
        if (entry.detexFormat == detexFormat)
            return entry.nriFormat;
    }

    return nri::Format::UNKNOWN;
}

namespace utils
{
    static void PostProcessTexture(const std::string& name,
                                   Texture& texture,
                                   bool computeAvgColorAndAlphaMode,
                                   detexTexture** dTexture,
                                   int mipNum)
    {
        texture.mips = (Mip*) dTexture;
        texture.name = name;
        texture.format = GetFormatNRI(dTexture[0]->format);
        texture.width = (uint16_t) dTexture[0]->width;
        texture.height = (uint16_t) dTexture[0]->height;
        texture.mipNum = (uint8_t) mipNum;

        // TODO: detex doesn't support cubemaps and 3D textures
        texture.layerNum = 1;
        texture.depth = 1;

        texture.alphaMode = AlphaMode::OPAQUE;
        if (computeAvgColorAndAlphaMode)
        {
            // Alpha mode
            if (texture.format == nri::Format::BC1_RGBA_UNORM || texture.format == nri::Format::BC1_RGBA_SRGB)
            {
                bool hasTransparency = false;
                for (int i = mipNum - 1; i >= 0 && !hasTransparency; i--)
                {
                    const size_t size = detexTextureSize(dTexture[i]->width_in_blocks,
                                                         dTexture[i]->height_in_blocks,
                                                         dTexture[i]->format);
                    const uint8_t* bc1 = dTexture[i]->data;

                    for (size_t j = 0; j < size && !hasTransparency; j += 8)
                    {
                        const uint16_t* c = (uint16_t*) bc1;
                        if (c[0] <= c[1])
                        {
                            const uint32_t bits = *(uint32_t*) (bc1 + 4);
                            for (uint32_t k = 0; k < 32 && !hasTransparency; k += 2)
                                hasTransparency = ((bits >> k) & 0x3) == 0x3;
                        }
                        bc1 += 8;
                    }
                }

                if (hasTransparency)
                    texture.alphaMode = AlphaMode::PREMULTIPLIED;
            }

            // Decompress last mip
            std::vector<uint8_t> image;
            detexTexture* lastMip = dTexture[mipNum - 1];
            uint8_t* rgba8 = lastMip->data;
            if (lastMip->format != DETEX_PIXEL_FORMAT_RGBA8)
            {
                // Convert to RGBA8 if the texture is compressed
                image.resize(lastMip->width * lastMip->height * detexGetPixelSize(DETEX_PIXEL_FORMAT_RGBA8));
                detexDecompressTextureLinear(lastMip, &image[0], DETEX_PIXEL_FORMAT_RGBA8);
                rgba8 = &image[0];
            }

            // Average color
            float4 avgColor = float4::Zero();
            const size_t pixelNum = lastMip->width * lastMip->height;
            for (size_t i = 0; i < pixelNum; i++)
                avgColor += Packing::unorm_to_float4<8, 8, 8, 8>(*(uint32_t*) (rgba8 + i * 4));
            avgColor /= float(pixelNum);

            if (texture.alphaMode != AlphaMode::PREMULTIPLIED && avgColor.w < 254.0f / 255.0f)
                texture.alphaMode = AlphaMode::TRANSPARENT;

            if (texture.alphaMode == AlphaMode::TRANSPARENT && avgColor.w == 0.0f)
            {
                printf("WARNING: Texture '%s' is fully transparent!\n", name.c_str());
                texture.alphaMode = AlphaMode::OFF;
            }
        }
    }
} // namespace utils

bool utils::LoadTexture(const std::string& path, Texture& texture, bool computeAvgColorAndAlphaMode)
{
    printf("Loading texture '%s'...\n", GetFileName(path));

    detexTexture** dTexture = nullptr;
    int mipNum = 0;

    if (!detexLoadTextureFileWithMipmaps(path.c_str(), 32, &dTexture, &mipNum))
    {
        printf("ERROR: Can't load texture '%s'\n", path.c_str());

        return false;
    }

    PostProcessTexture(path, texture, computeAvgColorAndAlphaMode, dTexture, mipNum);

    return true;
}

bool utils::LoadTextureFromMemory(
        const std::string& name, const uint8_t* data, int dataSize, Texture& texture, bool computeAvgColorAndAlphaMode)
{
    printf("Loading embedded texture '%s'...\n", name.c_str());

    int x, y, comp;
    unsigned char* image = stbi_load_from_memory((stbi_uc const*) data, dataSize, &x, &y, &comp, STBI_rgb_alpha);
    if (!image)
    {
        printf("Could not read memory for embedded texture %s. Reason: %s", name.c_str(), stbi_failure_reason());
        return false;
    }
    detexTexture** dTexture = (detexTexture**) malloc(sizeof(detexTexture*));
    dTexture[0] = (detexTexture*) malloc(sizeof(detexTexture));
    dTexture[0]->format = DETEX_PIXEL_FORMAT_RGBA8;
    dTexture[0]->width = x;
    dTexture[0]->height = y;
    dTexture[0]->width_in_blocks = x;
    dTexture[0]->height_in_blocks = y;
    size_t size = x * y * detexGetPixelSize(DETEX_PIXEL_FORMAT_RGBA8);
    dTexture[0]->data = (uint8_t*) malloc(size);
    memcpy(dTexture[0]->data, image, size);
    stbi_image_free(image);

    const int kMipNum = 1;
    PostProcessTexture(name, texture, computeAvgColorAndAlphaMode, dTexture, kMipNum);
    return true;
}

void utils::LoadTextureFromMemory(
        nri::Format format, uint32_t width, uint32_t height, const uint8_t* pixels, Texture& texture)
{
    assert(format == nri::Format::R8_UNORM);

    detexTexture** dTexture;
    detexLoadTextureFromMemory(DETEX_PIXEL_FORMAT_R8, width, height, pixels, &dTexture);

    texture.mipNum = 1;
    texture.layerNum = 1;
    texture.depth = 1;
    texture.format = format;
    texture.alphaMode = AlphaMode::OPAQUE;
    texture.mips = (Mip*) dTexture;
}
