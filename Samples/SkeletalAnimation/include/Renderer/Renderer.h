#pragma once
#include <string>
#include <memory>

#include <Model/Model.h>
#include <Platform/NativeWindowHandle.h>

namespace RAnimation
{
    class Renderer final
    {
    public:
        Renderer(NativeWindowHandle* window);
        bool Init(unsigned int width, unsigned int height);
        void SetSize(unsigned int width, unsigned int height);

        bool Draw(float deltaTime);

        void handleKeyEvents(int key, int scancode, int action, int mods);
        void handleMouseButtonEvents(int button, int action, int mods);
        void handleMousePositionEvents(double xPos, double yPos);

        bool hasModel(std::string modelFileName);
        std::shared_ptr<Model> getModel(std::string modelFileName);
        bool addModel(std::string modelFileName);
        void deleteModel(std::string modelFileName);

        std::shared_ptr<Instance> addInstance(std::shared_ptr<Model> model);
        void addInstances(std::shared_ptr<Model> model, int numInstances);
        void deleteInstance(std::shared_ptr<Instance> instance);
        void cloneInstance(std::shared_ptr<Instance> instance);

        void cleanup();
    };
} // namespace RAnimation