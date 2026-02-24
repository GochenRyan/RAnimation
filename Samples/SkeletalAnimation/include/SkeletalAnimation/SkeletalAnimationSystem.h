// NRI骨骼动画播放实现模板
// Week 1 周二实践日 - 实现简单的骨骼动画播放功能
// 基于试学内容中的完整示例代码简化而来

#include <NRI.h>
#include <NRIDeviceCreation.h>
#include <NRIHelper.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

// ============================================================================
// 常量定义
// ============================================================================

constexpr uint32_t MAX_BONE_COUNT = 64;          // 最大骨骼数量
constexpr uint32_t MAX_VERTEX_COUNT = 4096;      // 最大顶点数量
constexpr uint32_t MAX_INDEX_COUNT = 8192;       // 最大索引数量
constexpr uint32_t MAX_KEYFRAME_COUNT = 10;      // 最大关键帧数量

// ============================================================================
// 数据结构定义
// ============================================================================

// 骨骼变换数据
struct BoneData {
    glm::mat4 localTransform;      // 局部变换矩阵
    glm::mat4 globalTransform;     // 全局变换矩阵
    int32_t parentIndex;          // 父骨骼索引（-1表示根骨骼）
};

// 动画关键帧
struct AnimationKeyframe {
    float timestamp;              // 时间戳（秒）
    glm::mat4 boneTransforms[MAX_BONE_COUNT]; // 各骨骼的变换矩阵
};

// 顶点数据结构（用于蒙皮）
struct SkinnedVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texcoord;
    glm::uvec4 boneIndices;            // 影响该顶点的骨骼索引（最多4个）
    float boneWeights;           // 对应权重（总和为1.0）
};

// ============================================================================
// 骨骼动画系统类
// ============================================================================

class SkeletalAnimationSystem {
public:
    SkeletalAnimationSystem(nri::Device& device, nri::CommandAllocator& commandAllocator)
        : m_device(device)
        , m_commandAllocator(commandAllocator)
        , m_currentTime(0.0f)
        , m_currentKeyframe(0)
        , m_nextKeyframe(1) {
        
        // TODO: 初始化资源
        InitializeResources();
        
        // TODO: 初始化动画数据
        InitializeAnimationData();
    }
    
    ~SkeletalAnimationSystem() {
        // TODO: 清理资源
        CleanupResources();
    }
    
    // 更新骨骼动画
    void Update(float deltaTime) {
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
    void Render(nri::CommandBuffer& commandBuffer) {
        // TODO: 绑定骨骼矩阵缓冲区到着色器
        // 1. 设置描述符表
        // 2. 绑定顶点/索引缓冲区
        // 3. 绘制调用
    }
    
private:
    // NRI设备相关
    nri::Device& m_device;
    nri::CommandAllocator& m_commandAllocator;
    
    // 资源句柄
    nri::Buffer* m_boneMatrixBuffer = nullptr;
    nri::Descriptor* m_boneMatrixSRV = nullptr;
    nri::Buffer* m_vertexBuffer = nullptr;
    nri::Buffer* m_indexBuffer = nullptr;
    
    // 动画数据
    std::vector<BoneData> m_boneData;
    std::vector<AnimationKeyframe> m_keyframes;
    float m_currentTime;
    uint32_t m_currentKeyframe;
    uint32_t m_nextKeyframe;
    float m_blendFactor;
    
    // ============================================================================
    // 私有方法 - 需要你实现的部分
    // ============================================================================
    
    void InitializeResources() {
        // TODO: 创建骨骼矩阵缓冲区
        // 1. 创建结构化缓冲区（BufferUsage::SHADER_RESOURCE）
        // 2. 创建SRV描述符
        // 3. 创建顶点/索引缓冲区（用于测试网格）
        
        // 提示：参考试学内容中的nriCreateBuffer和nriCreateDescriptor调用
    }
    
    void InitializeAnimationData() {
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
    
    void UpdateBoneTransforms() {
        // TODO: 计算当前帧的骨骼变换矩阵
        // 1. 对每个骨骼，插值当前关键帧和下一关键帧的变换矩阵
        // 2. 从根骨骼开始，递归计算全局变换矩阵
        // 3. 存储最终骨骼矩阵到m_boneData[i].globalTransform
        
        // 提示：使用矩阵线性插值（实际项目中可能需要四元数插值）
        // globalTransform = parentGlobalTransform × localTransform
    }
    
    void UpdateGPUBuffers() {
        // TODO: 更新GPU骨骼矩阵缓冲区
        // 1. 将m_boneData中的globalTransform矩阵复制到映射内存
        // 2. 使用NRI的缓冲区更新机制
        
        // 提示：使用nri::BufferUploadHelper或直接内存映射
    }
    
    void CleanupResources() {
        // TODO: 释放所有NRI资源
        // 释放缓冲区、描述符等
    }
    
    // ============================================================================
    // 辅助函数
    // ============================================================================
    
    // 矩阵线性插值（简化版，实际项目可能需要四元数插值）
    glm::mat4 LerpMatrix(const glm::mat4& a, const glm::mat4& b, float t) {
        // GLM矩阵可以通过下标访问，矩阵按列存储：mat4[col][row]
        glm::mat4 result;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                result[i][j] = a[i][j] * (1.0f - t) + b[i][j] * t;
            }
        }
        return result;
    }
    
    // 创建简单的测试网格（圆柱体模拟肢体）
    void CreateTestMesh(std::vector<SkinnedVertex>& vertices, std::vector<uint32_t>& indices) {
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