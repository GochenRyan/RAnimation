#include <Platform/SDL/SDLPlatform.h>
#include <Application/Application.h>

using namespace RAnimation;

bool Application::init(unsigned int width, unsigned int height, std::string title)
{
    mPlatform = std::make_unique<SDLPlatform>();
    if (!mPlatform->Initialize())
    {
        return false;
    }

    WindowDesc windowDesc{};
    windowDesc.width = width;
    windowDesc.height = height;
    windowDesc.title = title;
    windowDesc.resizable = true;
    windowDesc.highDPI = true;
    windowDesc.maximized = false;

    auto window = mPlatform->CreateWindow(windowDesc);
    if (!window)
    {
        return false;
    }

    mRenderer = std::make_unique<Renderer>(window->GetNativeHandle());
    if (!mRenderer->Init(width, height))
    {
        return false;
    }
    return false;
}

void Application::MainLoop()
{
    std::chrono::time_point<std::chrono::steady_clock> loopStartTime = std::chrono::steady_clock::now();
    std::chrono::time_point<std::chrono::steady_clock> loopEndTime = std::chrono::steady_clock::now();
    float deltaTime = 0.0f;

    while (!mPlatform->GetMainWindow()->ShouldClose())
    {
        if (!mRenderer->Draw(deltaTime))
        {
            break;
        }

        mPlatform->PumpEvents();

        /* calculate the time we needed for the current frame, feed it to the next draw() call */
        loopEndTime = std::chrono::steady_clock::now();

        /* delta time in seconds */
        deltaTime = std::chrono::duration_cast<std::chrono::microseconds>(loopEndTime - loopStartTime).count() /
                    1'000'000.0f;

        loopStartTime = loopEndTime;
    }
}

void Application::Cleanup()
{
    if (mRenderer)
    {
        mRenderer->Cleanup();
        mRenderer = nullptr;
    }

    if (mPlatform)
    {
        mPlatform->Shutdown();
        mPlatform = nullptr;
    }
}
