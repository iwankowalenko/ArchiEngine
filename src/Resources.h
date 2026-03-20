#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Math2D.h"

namespace archi
{
    using MeshHandle = std::uint32_t;
    using TextureHandle = std::uint32_t;
    using ShaderHandle = std::uint32_t;

    constexpr MeshHandle InvalidMeshHandle = 0;
    constexpr TextureHandle InvalidTextureHandle = 0;
    constexpr ShaderHandle InvalidShaderHandle = 0;

    enum class ResourceLoadState
    {
        Unloaded,
        Loading,
        Ready,
        Failed
    };

    struct Vertex
    {
        Vec3 position{ 0.0f, 0.0f, 0.0f };
        Vec3 normal{ 0.0f, 0.0f, 1.0f };
        Vec2 uv{ 0.0f, 0.0f };
    };

    struct MeshData
    {
        std::vector<Vertex> vertices{};
        std::vector<std::uint32_t> indices{};
        Vec3 boundsMin{ 0.0f, 0.0f, 0.0f };
        Vec3 boundsMax{ 0.0f, 0.0f, 0.0f };
    };

    struct TextureData
    {
        int width = 0;
        int height = 0;
        int channels = 4;
        std::vector<unsigned char> pixels{};
    };

    struct ShaderSource
    {
        std::filesystem::path vertexPath{};
        std::filesystem::path fragmentPath{};
        std::string vertexCode{};
        std::string fragmentCode{};
        bool hotReloadEnabled = false;
    };

    struct MaterialAsset
    {
        std::filesystem::path sourcePath{};
        std::filesystem::file_time_type sourceWriteTime{};
        std::string shaderAsset{ "shaders/textured.shader.json" };
        std::string textureAsset{};
        Vec4 baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool useTexture = true;
        bool placeholder = false;
        bool hotReloadEnabled = true;
    };

    struct MeshAsset
    {
        std::filesystem::path sourcePath{};
        std::filesystem::path binaryCachePath{};
        std::filesystem::file_time_type sourceWriteTime{};
        MeshData mesh{};
        MeshHandle gpuHandle = InvalidMeshHandle;
        std::string importedMaterialAsset{};
        bool placeholder = false;
        bool hotReloadEnabled = true;
    };

    struct TextureAsset
    {
        std::filesystem::path sourcePath{};
        std::filesystem::path binaryCachePath{};
        std::filesystem::file_time_type sourceWriteTime{};
        int width = 0;
        int height = 0;
        int channels = 4;
        TextureHandle gpuHandle = InvalidTextureHandle;
        bool placeholder = false;
        bool hotReloadEnabled = true;
    };

    struct ShaderAsset
    {
        std::filesystem::path manifestPath{};
        std::filesystem::path vertexPath{};
        std::filesystem::path fragmentPath{};
        std::filesystem::file_time_type manifestWriteTime{};
        std::filesystem::file_time_type vertexWriteTime{};
        std::filesystem::file_time_type fragmentWriteTime{};
        ShaderHandle gpuHandle = InvalidShaderHandle;
        bool hotReloadEnabled = false;
        bool placeholder = false;
    };

    template <typename T>
    struct Resource
    {
        std::string key{};
        ResourceLoadState state = ResourceLoadState::Unloaded;
        std::string error{};
        T value{};

        bool IsReady() const noexcept { return state == ResourceLoadState::Ready; }
        bool IsLoading() const noexcept { return state == ResourceLoadState::Loading; }
        bool HasFailed() const noexcept { return state == ResourceLoadState::Failed; }
    };

    template <typename T>
    using ResourcePtr = std::shared_ptr<Resource<T>>;
}
