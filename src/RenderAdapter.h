#pragma once

#include <cstdint>
#include <string>

#include "Math2D.h"
#include "Resources.h"

namespace archi
{
    enum class Key
    {
        Up,
        Down,
        Left,
        Right,
        W,
        A,
        S,
        D,
        Q,
        E,
        Enter,
        Escape,
        P,
        LeftShift,
        N,
        R,
        K,
        L
    };

    enum class MouseButton
    {
        Left,
        Right
    };

    struct RenderConfig
    {
        int width = 1280;
        int height = 720;
        std::string title = "ArchiEngine";
        bool vsync = true;
    };

    struct RenderMeshCommand
    {
        MeshHandle mesh = InvalidMeshHandle;
        TextureHandle texture = InvalidTextureHandle;
        ShaderHandle shader = InvalidShaderHandle;
        Mat4 model = Mat4::Identity();
        Mat4 view = Mat4::Identity();
        Mat4 projection = Mat4::Identity();
        Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool useTexture = true;
    };

    class IRenderAdapter
    {
    public:
        virtual ~IRenderAdapter() = default;

        virtual bool Init(const RenderConfig& cfg) = 0;
        virtual void Shutdown() = 0;

        virtual void BeginFrame() = 0;
        virtual MeshHandle UploadMesh(const MeshData& mesh) = 0;
        virtual bool ReloadMesh(MeshHandle handle, const MeshData& mesh) = 0;
        virtual TextureHandle CreateTexture(const TextureData& texture) = 0;
        virtual bool ReloadTexture(TextureHandle handle, const TextureData& texture) = 0;
        virtual ShaderHandle CreateShaderProgram(const ShaderSource& shaderSource) = 0;
        virtual bool ReloadShaderProgram(ShaderHandle handle, const ShaderSource& shaderSource) = 0;
        virtual void DrawMesh(const RenderMeshCommand& command) = 0;
        virtual void EndFrame() = 0;

        virtual void PollEvents() = 0;
        virtual bool ShouldClose() const = 0;

        virtual int DrawableWidth() const = 0;
        virtual int DrawableHeight() const = 0;

        virtual float AspectRatio() const
        {
            const int height = DrawableHeight();
            return height > 0 ? static_cast<float>(DrawableWidth()) / static_cast<float>(height) : 1.0f;
        }

        virtual bool IsKeyDown(Key key) const = 0;
        virtual bool IsMouseButtonDown(MouseButton button) const = 0;

        // Positive = wheel up, negative = wheel down. Returns accumulated delta and resets it to 0.
        virtual float ConsumeScrollDeltaY() = 0;

        // Optional features (default: unsupported).
        virtual bool OpenAdditionalWindow(const RenderConfig& /*cfg*/, float /*clearR*/, float /*clearG*/, float /*clearB*/)
        {
            return false;
        }

        virtual void RequestShaderReload() {}
    };
}

