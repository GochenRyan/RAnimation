#include <Application/Application.h>

using namespace RAnimation;

int main(int argc, char* argv[])
{
    Application app;
    if (!app.init(1280, 720, "Skeletal Animation Sample"))
    {
        return -1;
    }

    app.MainLoop();
    app.Cleanup();

    return 0;
}