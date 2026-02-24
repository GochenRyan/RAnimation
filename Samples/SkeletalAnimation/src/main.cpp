#include <SkeletalAnimation/SkeletalAnimationSystem.h>

// ============================================================================
// 主程序框架
// ============================================================================

int main() {
    // NRI设备初始化
    nri::Device* device = nullptr;
    nri::CommandAllocator* commandAllocator = nullptr;
    
    // TODO: 初始化NRI设备（选择D3D12或Vulkan后端）
    
    // 创建动画系统
    SkeletalAnimationSystem animationSystem(*device, *commandAllocator);
    
    // 简单渲染循环模拟
    float deltaTime = 1.0f / 60.0f; // 假设60FPS
    for (int frame = 0; frame < 360; ++frame) { // 模拟6秒动画
        // 更新动画
        animationSystem.Update(deltaTime);
        
        // 渲染（简化）
        // TODO: 实际渲染调用
        
        // 模拟帧间隔
    }
    
    return 0;
}