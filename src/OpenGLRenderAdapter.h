#pragma once

#include "RenderAdapter.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace archi
{
    class OpenGLRenderAdapter final : public IRenderAdapter
    {
    public:
        struct MeshResource
        {
            unsigned int vbo = 0;
            unsigned int ebo = 0;
            int vertexCount = 0;
            int indexCount = 0;
            bool indexed = false;
            unsigned int drawMode = 0;
        };

        struct TextureResource
        {
            unsigned int glId = 0;
            int width = 0;
            int height = 0;
        };

        struct ShaderResource
        {
            unsigned int program = 0;
            int uModelLoc = -1;
            int uViewLoc = -1;
            int uProjectionLoc = -1;
            int uColorLoc = -1;
            int uUseTextureLoc = -1;
            int uTextureLoc = -1;
        };

        ~OpenGLRenderAdapter() override;

        bool Init(const RenderConfig& cfg) override;
        void Shutdown() override;

        void BeginFrame() override;
        MeshHandle UploadMesh(const MeshData& mesh) override;
        bool ReloadMesh(MeshHandle handle, const MeshData& mesh) override;
        TextureHandle CreateTexture(const TextureData& texture) override;
        bool ReloadTexture(TextureHandle handle, const TextureData& texture) override;
        ShaderHandle CreateShaderProgram(const ShaderSource& shaderSource) override;
        bool ReloadShaderProgram(ShaderHandle handle, const ShaderSource& shaderSource) override;
        void DrawMesh(const RenderMeshCommand& command) override;
        void DrawDebugBox(const RenderDebugBoxCommand& command) override;
        void DrawDebugSphere(const RenderDebugSphereCommand& command) override;
        void EndFrame() override;

        void PollEvents() override;
        bool ShouldClose() const override;
        int DrawableWidth() const override;
        int DrawableHeight() const override;

        bool IsKeyDown(Key key) const override;
        bool IsMouseButtonDown(MouseButton button) const override;
        Vec2 MousePosition() const override;
        float ConsumeScrollDeltaY() override;

        bool OpenAdditionalWindow(const RenderConfig& cfg, float clearR, float clearG, float clearB) override;

    private:
        struct WindowData;

        bool InitGLObjects();
        void DestroyGLObjects();
        bool InitDebugObjects();
        void DestroyDebugObjects();

        unsigned int CompileShader(unsigned int type, const char* src);
        unsigned int LinkProgram(unsigned int vs, unsigned int fs);
        ShaderResource BuildShaderResource(const ShaderSource& shaderSource);
        bool BuildMeshResource(const MeshData& mesh, MeshResource& outResource);

        unsigned int CreateVaoForCurrentContext(const MeshResource& mesh);
        unsigned int GetOrCreateVao(WindowData& window, MeshHandle meshHandle, const MeshResource& mesh);
        unsigned int CreateDebugVaoForCurrentContext();
        unsigned int CreateDebugCircleVaoForCurrentContext();
        void DestroyWindowVaos(WindowData& window);
        void DestroyMeshVaos(MeshHandle meshHandle);

        void OnKeyEvent(int glfwKey, int action);
        void OnMouseButtonEvent(int glfwButton, int action);
        void OnCursorPositionEvent(double xpos, double ypos);
        void OnScrollEvent(double yoffset);

        static constexpr std::size_t KeyCount = static_cast<std::size_t>(Key::Count);
        static constexpr std::size_t MouseCount = 2;
        static std::size_t KeyToIndex(Key key);
        static std::size_t MouseToIndex(MouseButton button);

        struct DebugBoxResource
        {
            unsigned int boxVbo = 0;
            unsigned int boxEbo = 0;
            unsigned int circleVbo = 0;
            int circleVertexCount = 0;
            unsigned int program = 0;
            int uModelLoc = -1;
            int uViewLoc = -1;
            int uProjectionLoc = -1;
            int uColorLoc = -1;
        };

    private:
        struct WindowData
        {
            GLFWwindow* window = nullptr;
            float clearR = 0.08f;
            float clearG = 0.08f;
            float clearB = 0.10f;
            std::unordered_map<MeshHandle, unsigned int> vaos{};
            unsigned int debugBoxVao = 0;
            unsigned int debugCircleVao = 0;
        };

        std::vector<WindowData> m_windows{};
        RenderConfig m_cfg{};

        std::array<unsigned char, KeyCount> m_keyDown{};
        std::array<unsigned char, MouseCount> m_mouseDown{};
        Vec2 m_mousePosition{};
        float m_scrollYAccum = 0.0f;
        int m_windowCounter = 1;

        MeshHandle m_nextMeshHandle = 1;
        TextureHandle m_nextTextureHandle = 1;
        ShaderHandle m_nextShaderHandle = 1;
        std::unordered_map<MeshHandle, MeshResource> m_meshes{};
        std::unordered_map<TextureHandle, TextureResource> m_textures{};
        std::unordered_map<ShaderHandle, ShaderResource> m_shaders{};
        DebugBoxResource m_debugBox{};
    };
}

