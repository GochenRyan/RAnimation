#pragma once
#include <memory>
#include <string>

#include <Renderer/Renderer.h>

namespace RAnimation
{
    class Application final
    {
    public:
        bool init(unsigned int width, unsigned int height, std::string title);
        void MainLoop();
        void Cleanup();
    private:
        std::unique_ptr<Renderer> mRenderer = nullptr;
    };
} // namespace RAnimation