#pragma once

#include "RenderAdapter.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

struct GLFWwindow;

namespace archi
{
    class OpenGLRenderAdapter final : public IRenderAdapter
    {
    public:
        ~OpenGLRenderAdapter() override;

        bool Init(const RenderConfig& cfg) override;
        void Shutdown() override;

        void BeginFrame() override;
        void DrawTestPrimitive(const Transform2D& transform) override;
        void EndFrame() override;

        void PollEvents() override;
        bool ShouldClose() const override;

        bool IsKeyDown(Key key) const override;
        bool IsMouseButtonDown(MouseButton button) const override;
        float ConsumeScrollDeltaY() override;

        bool OpenAdditionalWindow(const RenderConfig& cfg, float clearR, float clearG, float clearB) override;
        void RequestShaderReload() override;

    private:
        bool InitGLObjects();
        void DestroyGLObjects();

        bool BuildTestShaderProgram();
        bool LoadShaderSourcesFromFiles(std::string& outVs, std::string& outFs);
        void UpdateShaderHotReload();

        unsigned int CompileShader(unsigned int type, const char* src);
        unsigned int LinkProgram(unsigned int vs, unsigned int fs);

        void OnKeyEvent(int glfwKey, int action);
        void OnMouseButtonEvent(int glfwButton, int action);
        void OnScrollEvent(double yoffset);

        static constexpr std::size_t KeyCount = 10;
        static constexpr std::size_t MouseCount = 2;
        static std::size_t KeyToIndex(Key key);
        static std::size_t MouseToIndex(MouseButton button);

    private:
        struct WindowData
        {
            GLFWwindow* window = nullptr;
            float clearR = 0.08f;
            float clearG = 0.08f;
            float clearB = 0.10f;
            unsigned int vao = 0; // VAOs are not reliably shared across contexts
        };

        std::vector<WindowData> m_windows{};
        RenderConfig m_cfg{};

        std::array<unsigned char, KeyCount> m_keyDown{};
        std::array<unsigned char, MouseCount> m_mouseDown{};
        float m_scrollYAccum = 0.0f;
        int m_windowCounter = 1;

        unsigned int m_vbo = 0;
        unsigned int m_program = 0;
        int m_uMvpLoc = -1;

        bool m_shaderReloadRequested = false;
        std::filesystem::path m_vsPath{};
        std::filesystem::path m_fsPath{};
        std::filesystem::file_time_type m_vsLastWrite{};
        std::filesystem::file_time_type m_fsLastWrite{};
    };
}

