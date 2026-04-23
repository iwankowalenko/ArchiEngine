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

        Vec2& operator-=(const Vec2& rhs)
        {
            x -= rhs.x;
            y -= rhs.y;
            return *this;
        }
    };

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Vec3() = default;
        Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

        Vec3 operator+(const Vec3& rhs) const { return { x + rhs.x, y + rhs.y, z + rhs.z }; }
        Vec3 operator-(const Vec3& rhs) const { return { x - rhs.x, y - rhs.y, z - rhs.z }; }
        Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
        Vec3 operator/(float s) const { return { x / s, y / s, z / s }; }
        Vec3& operator+=(const Vec3& rhs)
        {
            x += rhs.x;
            y += rhs.y;
            z += rhs.z;
            return *this;
        }

        Vec3& operator-=(const Vec3& rhs)
        {
            x -= rhs.x;
            y -= rhs.y;
            z -= rhs.z;
            return *this;
        }
    };

    struct Vec4
    {
        float x = 1.0f;
        float y = 1.0f;
        float z = 1.0f;
        float w = 1.0f;

        Vec4() = default;
        Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    };

    struct Transform2D
    {
        Vec2 position{ 0.0f, 0.0f };
        float rotationRadians = 0.0f;
        Vec2 scale{ 1.0f, 1.0f };
    };

    struct Mat4
    {
        std::array<float, 16> values{};

        Mat4()
        {
            values.fill(0.0f);
        }

        float& operator()(int row, int col)
        {
            return values[static_cast<std::size_t>(col) * 4 + static_cast<std::size_t>(row)];
        }

        float operator()(int row, int col) const
        {
            return values[static_cast<std::size_t>(col) * 4 + static_cast<std::size_t>(row)];
        }

        float* data() { return values.data(); }
        const float* data() const { return values.data(); }

        static Mat4 Identity()
        {
            Mat4 result{};
            result(0, 0) = 1.0f;
            result(1, 1) = 1.0f;
            result(2, 2) = 1.0f;
            result(3, 3) = 1.0f;
            return result;
        }
    };

    inline float Length(const Vec3& v)
    {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    inline float Length(const Vec2& v)
    {
        return std::sqrt(v.x * v.x + v.y * v.y);
    }

    inline float Dot(const Vec3& lhs, const Vec3& rhs)
    {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }

    inline Vec3 Cross(const Vec3& lhs, const Vec3& rhs)
    {
        return {
            lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x
        };
    }

    inline Vec3 Normalize(const Vec3& v)
    {
        const float len = Length(v);
        if (len <= 0.0001f)
            return { 0.0f, 0.0f, 0.0f };
        return { v.x / len, v.y / len, v.z / len };
    }

    inline Vec2 Normalize(const Vec2& v)
    {
        const float len = Length(v);
        if (len <= 0.0001f)
            return { 0.0f, 0.0f };
        return { v.x / len, v.y / len };
    }

    inline float Clamp(float value, float minValue, float maxValue)
    {
        return value < minValue ? minValue : (value > maxValue ? maxValue : value);
    }

    inline float Radians(float degrees)
    {
        return degrees * 0.01745329251994329577f;
    }

    inline float Degrees(float radians)
    {
        return radians * 57.295779513082320876f;
    }

    inline Mat4 operator*(const Mat4& lhs, const Mat4& rhs)
    {
        Mat4 result{};
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += lhs(row, k) * rhs(k, col);
                result(row, col) = sum;
            }
        }
        return result;
    }

    inline Mat4 MakeTranslationMatrix(const Vec3& translation)
    {
        Mat4 result = Mat4::Identity();
        result(0, 3) = translation.x;
        result(1, 3) = translation.y;
        result(2, 3) = translation.z;
        return result;
    }

    inline Mat4 MakeScaleMatrix(const Vec3& scale)
    {
        Mat4 result = Mat4::Identity();
        result(0, 0) = scale.x;
        result(1, 1) = scale.y;
        result(2, 2) = scale.z;
        return result;
    }

    inline Mat4 MakeRotationXMatrix(float radians)
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);

        Mat4 result = Mat4::Identity();
        result(1, 1) = c;
        result(1, 2) = -s;
        result(2, 1) = s;
        result(2, 2) = c;
        return result;
    }

    inline Mat4 MakeRotationYMatrix(float radians)
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);

        Mat4 result = Mat4::Identity();
        result(0, 0) = c;
        result(0, 2) = s;
        result(2, 0) = -s;
        result(2, 2) = c;
        return result;
    }

    inline Mat4 MakeRotationZMatrix(float radians)
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);

        Mat4 result = Mat4::Identity();
        result(0, 0) = c;
        result(0, 1) = -s;
        result(1, 0) = s;
        result(1, 1) = c;
        return result;
    }

    inline Mat4 MakeTransformMatrix(const Vec3& position, const Vec3& rotationRadians, const Vec3& scale)
    {
        const Mat4 translation = MakeTranslationMatrix(position);
        const Mat4 rotation =
            MakeRotationZMatrix(rotationRadians.z) *
            MakeRotationYMatrix(rotationRadians.y) *
            MakeRotationXMatrix(rotationRadians.x);
        const Mat4 scaling = MakeScaleMatrix(scale);
        return translation * rotation * scaling;
    }

    inline Mat4 MakeTransformMatrix(const Transform2D& t)
    {
        return MakeTransformMatrix(
            Vec3{ t.position.x, t.position.y, 0.0f },
            Vec3{ 0.0f, 0.0f, t.rotationRadians },
            Vec3{ t.scale.x, t.scale.y, 1.0f });
    }

    inline Mat4 MakeViewMatrix(const Vec3& position, const Vec3& rotationRadians)
    {
        const Mat4 inverseRotation =
            MakeRotationXMatrix(-rotationRadians.x) *
            MakeRotationYMatrix(-rotationRadians.y) *
            MakeRotationZMatrix(-rotationRadians.z);
        const Mat4 inverseTranslation = MakeTranslationMatrix({ -position.x, -position.y, -position.z });
        return inverseRotation * inverseTranslation;
    }

    inline Mat4 MakeOrthographicMatrix(
        float left,
        float right,
        float bottom,
        float top,
        float nearPlane,
        float farPlane)
    {
        Mat4 result = Mat4::Identity();
        result(0, 0) = 2.0f / (right - left);
        result(1, 1) = 2.0f / (top - bottom);
        result(2, 2) = -2.0f / (farPlane - nearPlane);
        result(0, 3) = -(right + left) / (right - left);
        result(1, 3) = -(top + bottom) / (top - bottom);
        result(2, 3) = -(farPlane + nearPlane) / (farPlane - nearPlane);
        return result;
    }

    inline Mat4 MakeOrthographicProjection(float aspectRatio, float halfHeight, float nearPlane, float farPlane)
    {
        const float halfWidth = halfHeight * aspectRatio;
        return MakeOrthographicMatrix(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
    }

    inline Mat4 MakePerspectiveProjection(float verticalFovRadians, float aspectRatio, float nearPlane, float farPlane)
    {
        const float tanHalfFov = std::tan(verticalFovRadians * 0.5f);

        Mat4 result{};
        result(0, 0) = 1.0f / (aspectRatio * tanHalfFov);
        result(1, 1) = 1.0f / tanHalfFov;
        result(2, 2) = -(farPlane + nearPlane) / (farPlane - nearPlane);
        result(2, 3) = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
        result(3, 2) = -1.0f;
        return result;
    }

    inline Mat4 MakeLookAtMatrix(const Vec3& eye, const Vec3& target, const Vec3& up)
    {
        const Vec3 forward = Normalize(target - eye);
        const Vec3 right = Normalize(Cross(forward, up));
        const Vec3 recalculatedUp = Cross(right, forward);

        Mat4 result = Mat4::Identity();
        result(0, 0) = right.x;
        result(0, 1) = right.y;
        result(0, 2) = right.z;
        result(0, 3) = -Dot(right, eye);

        result(1, 0) = recalculatedUp.x;
        result(1, 1) = recalculatedUp.y;
        result(1, 2) = recalculatedUp.z;
        result(1, 3) = -Dot(recalculatedUp, eye);

        result(2, 0) = -forward.x;
        result(2, 1) = -forward.y;
        result(2, 2) = -forward.z;
        result(2, 3) = Dot(forward, eye);
        return result;
    }

    inline Vec3 TransformPoint(const Mat4& matrix, const Vec3& point)
    {
        const float x =
            matrix(0, 0) * point.x +
            matrix(0, 1) * point.y +
            matrix(0, 2) * point.z +
            matrix(0, 3);
        const float y =
            matrix(1, 0) * point.x +
            matrix(1, 1) * point.y +
            matrix(1, 2) * point.z +
            matrix(1, 3);
        const float z =
            matrix(2, 0) * point.x +
            matrix(2, 1) * point.y +
            matrix(2, 2) * point.z +
            matrix(2, 3);
        const float w =
            matrix(3, 0) * point.x +
            matrix(3, 1) * point.y +
            matrix(3, 2) * point.z +
            matrix(3, 3);

        if (std::abs(w) > 0.0001f)
            return { x / w, y / w, z / w };
        return { x, y, z };
    }

    inline Mat4 Inverse(const Mat4& matrix)
    {
        const float* m = matrix.data();
        Mat4 inverse{};
        float* inv = inverse.data();

        inv[0] = m[5] * m[10] * m[15] -
            m[5] * m[11] * m[14] -
            m[9] * m[6] * m[15] +
            m[9] * m[7] * m[14] +
            m[13] * m[6] * m[11] -
            m[13] * m[7] * m[10];

        inv[4] = -m[4] * m[10] * m[15] +
            m[4] * m[11] * m[14] +
            m[8] * m[6] * m[15] -
            m[8] * m[7] * m[14] -
            m[12] * m[6] * m[11] +
            m[12] * m[7] * m[10];

        inv[8] = m[4] * m[9] * m[15] -
            m[4] * m[11] * m[13] -
            m[8] * m[5] * m[15] +
            m[8] * m[7] * m[13] +
            m[12] * m[5] * m[11] -
            m[12] * m[7] * m[9];

        inv[12] = -m[4] * m[9] * m[14] +
            m[4] * m[10] * m[13] +
            m[8] * m[5] * m[14] -
            m[8] * m[6] * m[13] -
            m[12] * m[5] * m[10] +
            m[12] * m[6] * m[9];

        inv[1] = -m[1] * m[10] * m[15] +
            m[1] * m[11] * m[14] +
            m[9] * m[2] * m[15] -
            m[9] * m[3] * m[14] -
            m[13] * m[2] * m[11] +
            m[13] * m[3] * m[10];

        inv[5] = m[0] * m[10] * m[15] -
            m[0] * m[11] * m[14] -
            m[8] * m[2] * m[15] +
            m[8] * m[3] * m[14] +
            m[12] * m[2] * m[11] -
            m[12] * m[3] * m[10];

        inv[9] = -m[0] * m[9] * m[15] +
            m[0] * m[11] * m[13] +
            m[8] * m[1] * m[15] -
            m[8] * m[3] * m[13] -
            m[12] * m[1] * m[11] +
            m[12] * m[3] * m[9];

        inv[13] = m[0] * m[9] * m[14] -
            m[0] * m[10] * m[13] -
            m[8] * m[1] * m[14] +
            m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] -
            m[12] * m[2] * m[9];

        inv[2] = m[1] * m[6] * m[15] -
            m[1] * m[7] * m[14] -
            m[5] * m[2] * m[15] +
            m[5] * m[3] * m[14] +
            m[13] * m[2] * m[7] -
            m[13] * m[3] * m[6];

        inv[6] = -m[0] * m[6] * m[15] +
            m[0] * m[7] * m[14] +
            m[4] * m[2] * m[15] -
            m[4] * m[3] * m[14] -
            m[12] * m[2] * m[7] +
            m[12] * m[3] * m[6];

        inv[10] = m[0] * m[5] * m[15] -
            m[0] * m[7] * m[13] -
            m[4] * m[1] * m[15] +
            m[4] * m[3] * m[13] +
            m[12] * m[1] * m[7] -
            m[12] * m[3] * m[5];

        inv[14] = -m[0] * m[5] * m[14] +
            m[0] * m[6] * m[13] +
            m[4] * m[1] * m[14] -
            m[4] * m[2] * m[13] -
            m[12] * m[1] * m[6] +
            m[12] * m[2] * m[5];

        inv[3] = -m[1] * m[6] * m[11] +
            m[1] * m[7] * m[10] +
            m[5] * m[2] * m[11] -
            m[5] * m[3] * m[10] -
            m[9] * m[2] * m[7] +
            m[9] * m[3] * m[6];

        inv[7] = m[0] * m[6] * m[11] -
            m[0] * m[7] * m[10] -
            m[4] * m[2] * m[11] +
            m[4] * m[3] * m[10] +
            m[8] * m[2] * m[7] -
            m[8] * m[3] * m[6];

        inv[11] = -m[0] * m[5] * m[11] +
            m[0] * m[7] * m[9] +
            m[4] * m[1] * m[11] -
            m[4] * m[3] * m[9] -
            m[8] * m[1] * m[7] +
            m[8] * m[3] * m[5];

        inv[15] = m[0] * m[5] * m[10] -
            m[0] * m[6] * m[9] -
            m[4] * m[1] * m[10] +
            m[4] * m[2] * m[9] +
            m[8] * m[1] * m[6] -
            m[8] * m[2] * m[5];

        const float determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (std::abs(determinant) <= 0.000001f)
            return Mat4::Identity();

        const float inverseDeterminant = 1.0f / determinant;
        for (float& value : inverse.values)
            value *= inverseDeterminant;
        return inverse;
    }
}

