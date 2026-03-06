#pragma once
#include <string>

namespace RAnimation
{
    class Application final
    {
    public:
        bool init(unsigned int width, unsigned int height, std::string title);
        void mainLoop();
        void cleanup();
    };
} // namespace RAnimation