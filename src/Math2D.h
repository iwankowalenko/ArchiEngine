#pragma once

#include <array>
#include <cmath>

namespace archi
{
    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;

        Vec2() = default;
        Vec2(float x_, float y_) : x(x_), y(y_) {}

        Vec2 operator+(const Vec2& rhs) const { return { x + rhs.x, y + rhs.y }; }
        Vec2 operator-(const Vec2& rhs) const { return { x - rhs.x, y - rhs.y }; }
        Vec2 operator*(float s) const { return { x * s, y * s }; }
        Vec2& operator+=(const Vec2& rhs)
        {
            x += rhs.x;
            y += rhs.y;
            return *this;
        }
    };

    struct Transform2D
    {
        Vec2 position{ 0.0f, 0.0f }; // In NDC units (-1..1)
        float rotationRadians = 0.0f;
        Vec2 scale{ 1.0f, 1.0f };
    };

    // Column-major 4x4 matrix suitable for OpenGL uniforms.
    inline std::array<float, 16> MakeTransformMatrix(const Transform2D& t)
    {
        const float c = std::cos(t.rotationRadians);
        const float s = std::sin(t.rotationRadians);

        const float sx = t.scale.x;
        const float sy = t.scale.y;

        // 2D affine transform in 4x4:
        // [ c*sx  -s*sy  0  tx ]
        // [ s*sx   c*sy  0  ty ]
        // [  0      0    1   0 ]
        // [  0      0    0   1 ]
        return {
            c * sx,  s * sx, 0.0f, 0.0f,
            -s * sy, c * sy, 0.0f, 0.0f,
            0.0f,    0.0f,   1.0f, 0.0f,
            t.position.x, t.position.y, 0.0f, 1.0f
        };
    }
}

