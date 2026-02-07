#include "vulkan/vkcontext.h"

#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>


int main()
{
    if (!glfwInit())
        throw std::runtime_error("[VkEngine] Failed to initialize GLFW.");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Vulkan Window", nullptr, nullptr);

    if (!window)
        throw std::runtime_error("[VkEngine] Failed to create GLFW window.");


    VkContext context;
    context.initialize(window);
    
    return 0;
}