#pragma once

#include <string>

#include "Math2D.h"

namespace archi
{
    enum class Key
    {
        Up,
        Down,
        Left,
        Right,
        Enter,
        Escape,
        P,
        LeftShift,
        N,
        R
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

    class IRenderAdapter
    {
    public:
        virtual ~IRenderAdapter() = default;

        virtual bool Init(const RenderConfig& cfg) = 0;
        virtual void Shutdown() = 0;

        virtual void BeginFrame() = 0;
        virtual void DrawTestPrimitive(const Transform2D& transform) = 0;
        virtual void EndFrame() = 0;

        virtual void PollEvents() = 0;
        virtual bool ShouldClose() const = 0;

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

