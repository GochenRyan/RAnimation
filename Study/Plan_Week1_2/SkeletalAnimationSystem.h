// NRI骨骼动画播放实现模板
// Week 1 周二实践日 - 实现简单的骨骼动画播放功能
// 基于试学内容中的完整示例代码简化而来
#pragma once
#include <RHIWrap/Utils.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <ml.h>
#include <vector>

template <typename T, uint32_t N> constexpr uint32_t GetCountOf(T const (&)[N])
{
    return N;
}

struct Vertex
{
    float position[3];
};

struct QueuedFrame
{
    nri::CommandAllocator* commandAllocatorGraphics;
    nri::CommandAllocator* commandAllocatorCompute;
    std::array<nri::CommandBuffer*, 3> commandBufferGraphics;
    nri::CommandBuffer* commandBufferCompute;
};

// Settings
constexpr bool D3D11_ENABLE_COMMAND_BUFFER_EMULATION = false;
constexpr bool D3D12_DISABLE_ENHANCED_BARRIERS = false;

// ============================================================================
// 常量定义
// ============================================================================

constexpr uint32_t MAX_BONE_COUNT = 64;     // 最大骨骼数量
constexpr uint32_t MAX_VERTEX_COUNT = 4096; // 最大顶点数量
constexpr uint32_t MAX_INDEX_COUNT = 8192;  // 最大索引数量
constexpr uint32_t MAX_KEYFRAME_COUNT = 10; // 最大关键帧数量

// ============================================================================
// 数据结构定义
// ============================================================================

// 骨骼变换数据
struct BoneData
{
    glm::mat4 localTransform;  // 局部变换矩阵
    glm::mat4 globalTransform; // 全局变换矩阵
    int32_t parentIndex;       // 父骨骼索引（-1表示根骨骼）
};

// 动画关键帧
struct AnimationKeyframe
{
    float timestamp;                          // 时间戳（秒）
    glm::mat4 boneTransforms[MAX_BONE_COUNT]; // 各骨骼的变换矩阵
};

// 顶点数据结构（用于蒙皮）
struct SkinnedVertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texcoord;
    glm::uvec4 boneIndices; // 影响该顶点的骨骼索引（最多4个）
    float boneWeights;      // 对应权重（总和为1.0）
};

// ============================================================================
// 骨骼动画系统类
// ============================================================================

class SkeletalAnimationSystem
{
public:
    SkeletalAnimationSystem() : m_currentTime(0.0f), m_currentKeyframe(0), m_nextKeyframe(1)
    {
        Initialize();

        // TODO: 初始化资源
        InitializeResources();

        // TODO: 初始化动画数据
        InitializeAnimationData();
    }

    ~SkeletalAnimationSystem()
    {
        // TODO: 清理资源
        CleanupResources();
    }

    // 更新骨骼动画
    void Update(float deltaTime)
    {
        m_currentTime += deltaTime;

        // TODO: 步骤1 - 计算动画进度和插值因子
        // 1. 获取动画总时长
        // 2. 计算归一化时间（循环播放）
        // 3. 查找当前关键帧和下一关键帧
        // 4. 计算插值因子（blendFactor）

        // TODO: 步骤2 - 更新骨骼变换矩阵
        UpdateBoneTransforms();

        // TODO: 步骤3 - 更新GPU缓冲区
        UpdateGPUBuffers();
    }

    // 渲染动画模型
    void Render(nri::CommandBuffer& commandBuffer)
    {
        // TODO: 绑定骨骼矩阵缓冲区到着色器
        // 1. 设置描述符表
        // 2. 绑定顶点/索引缓冲区
        // 3. 绘制调用
    }

private:
    // 动画数据
    std::vector<BoneData> m_boneData;
    std::vector<AnimationKeyframe> m_keyframes;
    float m_currentTime;
    uint32_t m_currentKeyframe;
    uint32_t m_nextKeyframe;
    float m_blendFactor;

    // inline uint8_t GetQueuedFrameNum() const {
    //     return m_Vsync ? 2 : 3;
    // }

    // inline const nri::Window& GetWindow() const {
    //     return m_NRIWindow;
    // }

    // inline uint2 GetOutputResolution() const {
    //     return m_OutputResolution;
    // }

    // inline uint8_t GetOptimalSwapChainTextureNum() const {
    //     return GetQueuedFrameNum() + 1;
    // }

    // ============================================================================
    // 私有方法 - 需要你实现的部分
    // ============================================================================
    void Initialize()
    {
        // nri::GraphicsAPI graphicsAPI = nri::GraphicsAPI::VK;
        // bool debugAPI = false;
        // bool debugNRI = false;
        // uint32_t adapterIndex = 0;

        // // Adapters
        // nri::AdapterDesc adapterDesc[2] = {};
        // uint32_t adapterDescsNum = 2;
        // NRI_ABORT_ON_FAILURE(nriEnumerateAdapters(adapterDesc, adapterDescsNum));

        // // Device
        // nri::QueueFamilyDesc queueFamilies[2] = {};
        // queueFamilies[0].queueNum = 1;
        // queueFamilies[0].queueType = nri::QueueType::GRAPHICS;
        // queueFamilies[1].queueNum = 1;
        // queueFamilies[1].queueType = nri::QueueType::COMPUTE;

        // nri::DeviceCreationDesc deviceCreationDesc = {};
        // deviceCreationDesc.graphicsAPI = graphicsAPI;
        // deviceCreationDesc.queueFamilies = queueFamilies;
        // deviceCreationDesc.queueFamilyNum = GetCountOf(queueFamilies);
        // deviceCreationDesc.enableGraphicsAPIValidation = m_DebugAPI;
        // deviceCreationDesc.enableNRIValidation = m_DebugNRI;
        // deviceCreationDesc.enableD3D11CommandBufferEmulation = D3D11_ENABLE_COMMAND_BUFFER_EMULATION;
        // deviceCreationDesc.disableD3D12EnhancedBarriers = D3D12_DISABLE_ENHANCED_BARRIERS;
        // deviceCreationDesc.vkBindingOffsets = VK_BINDING_OFFSETS;
        // deviceCreationDesc.adapterDesc = &adapterDesc[std::min(m_AdapterIndex, adapterDescsNum - 1)];
        // deviceCreationDesc.allocationCallbacks = m_AllocationCallbacks;
        // NRI_ABORT_ON_FAILURE(nri::nriCreateDevice(deviceCreationDesc, m_Device));

        // // NRI
        // NRI_ABORT_ON_FAILURE(nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::CoreInterface),
        // (nri::CoreInterface*)&NRI)); NRI_ABORT_ON_FAILURE(nri::nriGetInterface(*m_Device,
        // NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&NRI));
        // NRI_ABORT_ON_FAILURE(nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::StreamerInterface),
        // (nri::StreamerInterface*)&NRI)); NRI_ABORT_ON_FAILURE(nri::nriGetInterface(*m_Device,
        // NRI_INTERFACE(nri::SwapChainInterface), (nri::SwapChainInterface*)&NRI));

        // const nri::DeviceDesc& deviceDesc = NRI.GetDeviceDesc(*m_Device);

        // // Create streamer
        // nri::StreamerDesc streamerDesc = {};
        // streamerDesc.dynamicBufferMemoryLocation = nri::MemoryLocation::HOST_UPLOAD;
        // streamerDesc.dynamicBufferDesc = { 0, 0, nri::BufferUsageBits::VERTEX_BUFFER |
        // nri::BufferUsageBits::INDEX_BUFFER }; streamerDesc.constantBufferMemoryLocation =
        // nri::MemoryLocation::HOST_UPLOAD; streamerDesc.queuedFrameNum = GetQueuedFrameNum();
        // NRI_ABORT_ON_FAILURE(NRI.CreateStreamer(*m_Device, streamerDesc, m_Streamer));

        // // Command queues
        // NRI_ABORT_ON_FAILURE(NRI.GetQueue(*m_Device, nri::QueueType::GRAPHICS, 0, m_GraphicsQueue));
        // NRI.SetDebugName(m_GraphicsQueue, "GraphicsQueue");

        // NRI.GetQueue(*m_Device, nri::QueueType::COMPUTE, 0, m_ComputeQueue);
        // if (m_ComputeQueue)
        //     NRI.SetDebugName(m_ComputeQueue, "ComputeQueue");

        // m_HasComputeQueue = m_ComputeQueue && graphicsAPI != nri::GraphicsAPI::D3D11;
        // m_IsAsyncMode = m_HasComputeQueue;

        // // Fences
        // NRI_ABORT_ON_FAILURE(NRI.CreateFence(*m_Device, 0, m_ComputeFence));
        // NRI_ABORT_ON_FAILURE(NRI.CreateFence(*m_Device, 0, m_FrameFence));

        // // Swap chain
        // nri::Format swapChainFormat;
        // {
        //     nri::SwapChainDesc swapChainDesc = {};
        //     swapChainDesc.window = GetWindow();
        //     swapChainDesc.queue = m_GraphicsQueue;
        //     swapChainDesc.format = nri::SwapChainFormat::BT709_G22_8BIT;
        //     swapChainDesc.flags = (m_Vsync ? nri::SwapChainBits::VSYNC : nri::SwapChainBits::NONE) |
        //     nri::SwapChainBits::ALLOW_TEARING; swapChainDesc.width = (uint16_t)GetOutputResolution().x;
        //     swapChainDesc.height = (uint16_t)GetOutputResolution().y;
        //     swapChainDesc.textureNum = GetOptimalSwapChainTextureNum();
        //     swapChainDesc.queuedFrameNum = GetQueuedFrameNum();
        //     NRI_ABORT_ON_FAILURE(NRI.CreateSwapChain(*m_Device, swapChainDesc, m_SwapChain));

        //     uint32_t swapChainTextureNum;
        //     nri::Texture* const* swapChainTextures = NRI.GetSwapChainTextures(*m_SwapChain, swapChainTextureNum);

        //     swapChainFormat = NRI.GetTextureDesc(*swapChainTextures[0]).format;

        //     for (uint32_t i = 0; i < swapChainTextureNum; i++) {
        //         nri::Texture2DViewDesc textureViewDesc = { swapChainTextures[i],
        //         nri::Texture2DViewType::COLOR_ATTACHMENT, swapChainFormat };

        //         nri::Descriptor* colorAttachment = nullptr;
        //         NRI_ABORT_ON_FAILURE(NRI.CreateTexture2DView(textureViewDesc, colorAttachment));

        //         nri::Fence* acquireSemaphore = nullptr;
        //         NRI_ABORT_ON_FAILURE(NRI.CreateFence(*m_Device, nri::SWAPCHAIN_SEMAPHORE, acquireSemaphore));

        //         nri::Fence* releaseSemaphore = nullptr;
        //         NRI_ABORT_ON_FAILURE(NRI.CreateFence(*m_Device, nri::SWAPCHAIN_SEMAPHORE, releaseSemaphore));

        //         SwapChainTexture& swapChainTexture = m_SwapChainTextures.emplace_back();

        //         swapChainTexture = {};
        //         swapChainTexture.acquireSemaphore = acquireSemaphore;
        //         swapChainTexture.releaseSemaphore = releaseSemaphore;
        //         swapChainTexture.texture = swapChainTextures[i];
        //         swapChainTexture.colorAttachment = colorAttachment;
        //         swapChainTexture.attachmentFormat = swapChainFormat;
        //     }
        // }

        // // Queued frames
        // m_QueuedFrames.resize(GetQueuedFrameNum());
        // for (QueuedFrame& queuedFrame : m_QueuedFrames) {
        //     NRI_ABORT_ON_FAILURE(NRI.CreateCommandAllocator(*m_GraphicsQueue, queuedFrame.commandAllocatorGraphics));
        //     for (size_t i = 0; i < queuedFrame.commandBufferGraphics.size(); i++)
        //         NRI_ABORT_ON_FAILURE(NRI.CreateCommandBuffer(*queuedFrame.commandAllocatorGraphics,
        //         queuedFrame.commandBufferGraphics[i]));

        //     if (m_IsAsyncMode) {
        //         NRI_ABORT_ON_FAILURE(NRI.CreateCommandAllocator(*m_ComputeQueue,
        //         queuedFrame.commandAllocatorCompute));
        //         NRI_ABORT_ON_FAILURE(NRI.CreateCommandBuffer(*queuedFrame.commandAllocatorCompute,
        //         queuedFrame.commandBufferCompute));
        //     }
        // }

        // { // Pipeline layout
        //     nri::DescriptorRangeDesc descriptorRangeStorage = { 0, 1, nri::DescriptorType::STORAGE_TEXTURE,
        //     nri::StageBits::COMPUTE_SHADER };

        //     nri::DescriptorSetDesc descriptorSetDesc = { 0, &descriptorRangeStorage, 1 };

        //     nri::PipelineLayoutDesc pipelineLayoutDesc = {};
        //     pipelineLayoutDesc.descriptorSetNum = 1;
        //     pipelineLayoutDesc.descriptorSets = &descriptorSetDesc;
        //     pipelineLayoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER | nri::StageBits::VERTEX_SHADER |
        //     nri::StageBits::FRAGMENT_SHADER; NRI_ABORT_ON_FAILURE(NRI.CreatePipelineLayout(*m_Device,
        //     pipelineLayoutDesc, m_SharedPipelineLayout));
        // }

        // utils::ShaderCodeStorage shaderCodeStorage;
        // { // Graphics pipeline
        //     nri::VertexStreamDesc vertexStreamDesc = {};
        //     vertexStreamDesc.bindingSlot = 0;

        //     nri::VertexAttributeDesc vertexAttributeDesc[1] = {};
        //     {
        //         vertexAttributeDesc[0].format = nri::Format::RGB32_SFLOAT;
        //         vertexAttributeDesc[0].streamIndex = 0;
        //         vertexAttributeDesc[0].offset = offsetof(Vertex, position);
        //         vertexAttributeDesc[0].d3d = { "POSITION", 0 };
        //         vertexAttributeDesc[0].vk.location = { 0 };
        //     }

        //     nri::VertexInputDesc vertexInputDesc = {};
        //     vertexInputDesc.attributes = vertexAttributeDesc;
        //     vertexInputDesc.attributeNum = (uint8_t)GetCountOf(vertexAttributeDesc);
        //     vertexInputDesc.streams = &vertexStreamDesc;
        //     vertexInputDesc.streamNum = 1;

        //     nri::InputAssemblyDesc inputAssemblyDesc = {};
        //     inputAssemblyDesc.topology = nri::Topology::TRIANGLE_LIST;

        //     nri::RasterizationDesc rasterizationDesc = {};
        //     rasterizationDesc.fillMode = nri::FillMode::SOLID;
        //     rasterizationDesc.cullMode = nri::CullMode::NONE;

        //     nri::ColorAttachmentDesc colorAttachmentDesc = {};
        //     colorAttachmentDesc.format = swapChainFormat;
        //     colorAttachmentDesc.colorWriteMask = nri::ColorWriteBits::RGBA;

        //     nri::OutputMergerDesc outputMergerDesc = {};
        //     outputMergerDesc.colors = &colorAttachmentDesc;
        //     outputMergerDesc.colorNum = 1;

        //     nri::ShaderDesc shaderStages[] = {
        //         utils::LoadShader(deviceDesc.graphicsAPI, "sample_1.vs", shaderCodeStorage),
        //         utils::LoadShader(deviceDesc.graphicsAPI, "sample_1.fs", shaderCodeStorage),
        //     };

        //     nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
        //     graphicsPipelineDesc.pipelineLayout = m_SharedPipelineLayout;
        //     graphicsPipelineDesc.vertexInput = &vertexInputDesc;
        //     graphicsPipelineDesc.inputAssembly = inputAssemblyDesc;
        //     graphicsPipelineDesc.rasterization = rasterizationDesc;
        //     graphicsPipelineDesc.outputMerger = outputMergerDesc;
        //     graphicsPipelineDesc.shaders = shaderStages;
        //     graphicsPipelineDesc.shaderNum = GetCountOf(shaderStages);
        //     NRI_ABORT_ON_FAILURE(NRI.CreateGraphicsPipeline(*m_Device, graphicsPipelineDesc, m_GraphicsPipeline));
        // }
    }

    void InitializeResources()
    {
        // TODO: 创建骨骼矩阵缓冲区
        // 1. 创建结构化缓冲区（BufferUsage::SHADER_RESOURCE）
        // 2. 创建SRV描述符
        // 3. 创建顶点/索引缓冲区（用于测试网格）

        // 提示：参考试学内容中的nriCreateBuffer和nriCreateDescriptor调用

        // nri::BufferDesc boneBufferDesc{};
        // boneBufferDesc.size = MAX_BONE_COUNT * sizeof(glm::mat4);
        // boneBufferDesc.usage = nri::BufferUsageBits::SHADER_RESOURCE;

        // nri::Buffer* boneBuffer;
        // NRI.CreateBuffer(*m_device, boneBufferDesc, boneBuffer);

        // // 创建SRV描述符
        // nri::Descriptor* boneSRV;
    }

    void InitializeAnimationData()
    {
        // TODO: 初始化示例骨骼层级结构
        // 创建简单的3骨骼层级：Root -> Bone1 -> Bone2
        // 为每个骨骼设置：
        // - parentIndex: 父骨骼索引（Root为-1）
        // - localTransform: 局部变换矩阵（位置、旋转）
        // - globalTransform: 初始全局变换矩阵

        // TODO: 创建简单的动画关键帧
        // 创建2-3个关键帧，每帧包含所有骨骼的变换矩阵
        // 关键帧之间应有明显的姿态变化
    }

    void UpdateBoneTransforms()
    {
        // TODO: 计算当前帧的骨骼变换矩阵
        // 1. 对每个骨骼，插值当前关键帧和下一关键帧的变换矩阵
        // 2. 从根骨骼开始，递归计算全局变换矩阵
        // 3. 存储最终骨骼矩阵到m_boneData[i].globalTransform

        // 提示：使用矩阵线性插值（实际项目中可能需要四元数插值）
        // globalTransform = parentGlobalTransform × localTransform
    }

    void UpdateGPUBuffers()
    {
        // TODO: 更新GPU骨骼矩阵缓冲区
        // 1. 将m_boneData中的globalTransform矩阵复制到映射内存
        // 2. 使用NRI的缓冲区更新机制

        // 提示：使用nri::BufferUploadHelper或直接内存映射
    }

    void CleanupResources()
    {
        // TODO: 释放所有NRI资源
        // 释放缓冲区、描述符等
    }

    // ============================================================================
    // 辅助函数
    // ============================================================================

    // 矩阵线性插值（简化版，实际项目可能需要四元数插值）
    glm::mat4 LerpMatrix(const glm::mat4& a, const glm::mat4& b, float t)
    {
        // GLM矩阵可以通过下标访问，矩阵按列存储：mat4[col][row]
        glm::mat4 result;
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                result[i][j] = a[i][j] * (1.0f - t) + b[i][j] * t;
            }
        }
        return result;
    }

    // 创建简单的测试网格（圆柱体模拟肢体）
    void CreateTestMesh(std::vector<SkinnedVertex>& vertices, std::vector<uint32_t>& indices)
    {
        // TODO: 创建简单的测试网格
        // 可创建一个圆柱体或长方体，顶点绑定到相应的骨骼
        // 用于验证动画效果
    }
};

// ============================================================================
// 着色器代码模板（HLSL）
// ============================================================================

/*
// 骨骼矩阵常量缓冲区
cbuffer BoneMatrices : register(b0) {
    float4x4 g_boneMatrices[MAX_BONE_COUNT];
};

// 输入顶点结构
struct VS_INPUT {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
    uint4 boneIndices : BONEINDICES;
    float4 boneWeights : BONEWEIGHTS;
};

// 输出顶点结构
struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
};

// 蒙皮顶点着色器
VS_OUTPUT SkinVS(VS_INPUT input) {
    VS_OUTPUT output;

    // TODO: 实现矩阵调色板蒙皮计算
    // 1. 初始化变换后的位置和法线
    // 2. 遍历所有影响的骨骼（最多4个）
    // 3. 应用骨骼权重进行混合
    // 4. 变换顶点位置和法线

    // 提示：使用g_boneMatrices[boneIndex]变换顶点
    // 注意法线变换需要使用逆转置矩阵或3x3子矩阵

    return output;
}
*/

// ============================================================================
// 注意事项
// ============================================================================

// 1. 此代码为教学模板，实际项目中需要更完善的错误处理
// 2. 矩阵插值在实际项目中通常使用四元数/缩放向量分别插值
// 3. 骨骼矩阵缓冲区更新应考虑双缓冲或环状缓冲区避免GPU等待
// 4. 实际蒙皮着色器可能需要支持更多骨骼或使用纹理存储矩阵