#include "ResourceManager.h"

#include "AssetPaths.h"
#include "Logger.h"
#include "RenderAdapter.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <nlohmann/json.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <future>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace archi
{
    namespace
    {
        using json = nlohmann::json;

        constexpr std::uint32_t kMeshCacheMagic = 0x3148534d;    // MSH1
        constexpr std::uint32_t kTextureCacheMagic = 0x31525854; // TXR1
        constexpr std::uint32_t kMeshCacheVersion = 2;
        constexpr std::uint32_t kTextureCacheVersion = 1;
        constexpr const char* kPlaceholderMeshKey = "builtin:mesh:placeholder";
        constexpr const char* kBuiltInSphereMeshKey = "builtin:mesh:sphere";
        constexpr const char* kPlaceholderTextureKey = "builtin:texture:placeholder";
        constexpr const char* kPlaceholderMaterialKey = "builtin:material:placeholder";
        constexpr const char* kDefaultMaterialKey = "builtin:material:default";
        constexpr const char* kDefaultShaderAsset = "shaders/textured.shader.json";

        template <typename T>
        bool IsFutureReady(std::future<T>& future)
        {
            using namespace std::chrono_literals;
            return future.valid() && future.wait_for(0s) == std::future_status::ready;
        }

        std::string ReadTextFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::in | std::ios::binary);
            if (!file)
                return {};

            std::ostringstream stream;
            stream << file.rdbuf();
            return stream.str();
        }

        template <typename T>
        bool WritePod(std::ostream& stream, const T& value)
        {
            stream.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
            return static_cast<bool>(stream);
        }

        template <typename T>
        bool ReadPod(std::istream& stream, T& value)
        {
            stream.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)));
            return static_cast<bool>(stream);
        }

        bool WriteString(std::ostream& stream, const std::string& value)
        {
            const std::uint32_t length = static_cast<std::uint32_t>(value.size());
            return WritePod(stream, length) &&
                (length == 0 || static_cast<bool>(stream.write(value.data(), length)));
        }

        bool ReadString(std::istream& stream, std::string& value)
        {
            std::uint32_t length = 0;
            if (!ReadPod(stream, length))
                return false;

            value.resize(length);
            if (length > 0)
                stream.read(value.data(), length);
            return static_cast<bool>(stream);
        }

        std::string MakeAssetReference(const std::filesystem::path& path)
        {
            const std::filesystem::path absolute = std::filesystem::weakly_canonical(path);
            const std::filesystem::path assetsRoot = std::filesystem::weakly_canonical(FindAssetsRoot());

            std::error_code error{};
            const std::filesystem::path relative = std::filesystem::relative(absolute, assetsRoot, error);
            if (!error)
            {
                const std::string relativeString = relative.generic_string();
                if (!relativeString.empty() &&
                    !(relativeString.size() >= 2 && relativeString[0] == '.' && relativeString[1] == '.'))
                    return relativeString;
            }

            return absolute.generic_string();
        }

        std::uint64_t Fnv1a64(std::string_view value)
        {
            std::uint64_t hash = 14695981039346656037ull;
            for (const unsigned char ch : value)
            {
                hash ^= ch;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::string MakeHexString(std::uint64_t value)
        {
            std::ostringstream stream;
            stream << std::hex << value;
            return stream.str();
        }

        std::filesystem::path MakeBinaryCachePath(const std::string& key, const char* extension)
        {
            return GetWritableAssetPath(std::filesystem::path("cache") / (MakeHexString(Fnv1a64(key)) + extension));
        }

        std::filesystem::path ResolveRelativeTo(const std::filesystem::path& sourcePath, const std::string& rawPath)
        {
            const std::filesystem::path candidate(rawPath);
            if (candidate.is_absolute())
                return candidate.lexically_normal();
            return std::filesystem::weakly_canonical(sourcePath.parent_path() / candidate);
        }

        Vec3 ComputeFaceNormal(const Vertex& a, const Vertex& b, const Vertex& c)
        {
            const Vec3 ab = b.position - a.position;
            const Vec3 ac = c.position - a.position;
            const Vec3 cross{
                ab.y * ac.z - ab.z * ac.y,
                ab.z * ac.x - ab.x * ac.z,
                ab.x * ac.y - ab.y * ac.x
            };
            const Vec3 normal = Normalize(cross);
            return Length(normal) > 0.0f ? normal : Vec3{ 0.0f, 0.0f, 1.0f };
        }

        void UpdateBounds(const Vec3& point, Vec3& outMin, Vec3& outMax, bool& initialized)
        {
            if (!initialized)
            {
                outMin = point;
                outMax = point;
                initialized = true;
                return;
            }

            outMin.x = std::min(outMin.x, point.x);
            outMin.y = std::min(outMin.y, point.y);
            outMin.z = std::min(outMin.z, point.z);
            outMax.x = std::max(outMax.x, point.x);
            outMax.y = std::max(outMax.y, point.y);
            outMax.z = std::max(outMax.z, point.z);
        }

        Vec3 TransformPosition(const aiMatrix4x4& transform, const aiVector3D& position)
        {
            return {
                transform.a1 * position.x + transform.a2 * position.y + transform.a3 * position.z + transform.a4,
                transform.b1 * position.x + transform.b2 * position.y + transform.b3 * position.z + transform.b4,
                transform.c1 * position.x + transform.c2 * position.y + transform.c3 * position.z + transform.c4
            };
        }

        Vec3 TransformNormal(const aiMatrix4x4& transform, const aiVector3D& normal)
        {
            const aiMatrix3x3 basis(transform);
            aiMatrix3x3 normalMatrix = basis;
            normalMatrix.Inverse().Transpose();

            return Normalize({
                normalMatrix.a1 * normal.x + normalMatrix.a2 * normal.y + normalMatrix.a3 * normal.z,
                normalMatrix.b1 * normal.x + normalMatrix.b2 * normal.y + normalMatrix.b3 * normal.z,
                normalMatrix.c1 * normal.x + normalMatrix.c2 * normal.y + normalMatrix.c3 * normal.z
            });
        }

        void FinalizeMissingNormals(MeshData& data, std::size_t indexStart)
        {
            for (std::size_t i = indexStart; i + 2 < data.indices.size(); i += 3)
            {
                const std::uint32_t ia = data.indices[i + 0];
                const std::uint32_t ib = data.indices[i + 1];
                const std::uint32_t ic = data.indices[i + 2];
                if (ia >= data.vertices.size() || ib >= data.vertices.size() || ic >= data.vertices.size())
                    continue;

                const Vec3 faceNormal = ComputeFaceNormal(data.vertices[ia], data.vertices[ib], data.vertices[ic]);
                if (Length(data.vertices[ia].normal) <= 0.0001f)
                    data.vertices[ia].normal = faceNormal;
                if (Length(data.vertices[ib].normal) <= 0.0001f)
                    data.vertices[ib].normal = faceNormal;
                if (Length(data.vertices[ic].normal) <= 0.0001f)
                    data.vertices[ic].normal = faceNormal;
            }
        }

        void AppendAssimpMesh(
            const aiMesh& mesh,
            const aiMatrix4x4& worldTransform,
            MeshData& outData,
            Vec3& outBoundsMin,
            Vec3& outBoundsMax,
            bool& boundsInitialized)
        {
            const std::size_t baseVertex = outData.vertices.size();
            const std::size_t indexStart = outData.indices.size();

            for (unsigned int i = 0; i < mesh.mNumVertices; ++i)
            {
                Vertex vertex{};
                vertex.position = TransformPosition(worldTransform, mesh.mVertices[i]);
                if (mesh.HasNormals())
                    vertex.normal = TransformNormal(worldTransform, mesh.mNormals[i]);
                if (mesh.HasTextureCoords(0))
                    vertex.uv = { mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y };

                UpdateBounds(vertex.position, outBoundsMin, outBoundsMax, boundsInitialized);
                outData.vertices.push_back(vertex);
            }

            for (unsigned int faceIndex = 0; faceIndex < mesh.mNumFaces; ++faceIndex)
            {
                const aiFace& face = mesh.mFaces[faceIndex];
                if (face.mNumIndices < 3)
                    continue;

                for (unsigned int i = 0; i < face.mNumIndices; ++i)
                    outData.indices.push_back(static_cast<std::uint32_t>(baseVertex + face.mIndices[i]));
            }

            if (!mesh.HasNormals())
                FinalizeMissingNormals(outData, indexStart);
        }

        void AppendAssimpNodeMeshes(
            const aiScene& scene,
            const aiNode& node,
            const aiMatrix4x4& parentTransform,
            MeshData& outData,
            Vec3& outBoundsMin,
            Vec3& outBoundsMax,
            bool& boundsInitialized,
            int& outFirstMaterialIndex)
        {
            const aiMatrix4x4 worldTransform = parentTransform * node.mTransformation;

            for (unsigned int i = 0; i < node.mNumMeshes; ++i)
            {
                const unsigned int meshIndex = node.mMeshes[i];
                if (meshIndex >= scene.mNumMeshes || scene.mMeshes[meshIndex] == nullptr)
                    continue;

                const aiMesh& mesh = *scene.mMeshes[meshIndex];
                if (outFirstMaterialIndex < 0)
                    outFirstMaterialIndex = static_cast<int>(mesh.mMaterialIndex);
                AppendAssimpMesh(mesh, worldTransform, outData, outBoundsMin, outBoundsMax, boundsInitialized);
            }

            for (unsigned int childIndex = 0; childIndex < node.mNumChildren; ++childIndex)
            {
                if (node.mChildren[childIndex] == nullptr)
                    continue;

                AppendAssimpNodeMeshes(
                    scene,
                    *node.mChildren[childIndex],
                    worldTransform,
                    outData,
                    outBoundsMin,
                    outBoundsMax,
                    boundsInitialized,
                    outFirstMaterialIndex);
            }
        }

        bool ReadVec4(const json& value, Vec4& outValue)
        {
            if (!value.is_array() || value.size() != 4)
                return false;

            outValue = {
                value[0].get<float>(),
                value[1].get<float>(),
                value[2].get<float>(),
                value[3].get<float>()
            };
            return true;
        }

        bool WriteMeshBinaryCache(const std::filesystem::path& path, const MeshData& mesh, const MaterialAsset* material)
        {
            if (const auto parent = path.parent_path(); !parent.empty())
                std::filesystem::create_directories(parent);

            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file)
                return false;

            const std::uint8_t hasMaterial = material ? 1u : 0u;
            const std::uint32_t vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
            const std::uint32_t indexCount = static_cast<std::uint32_t>(mesh.indices.size());

            if (!WritePod(file, kMeshCacheMagic) ||
                !WritePod(file, kMeshCacheVersion) ||
                !WritePod(file, vertexCount) ||
                !WritePod(file, indexCount) ||
                !WritePod(file, mesh.boundsMin) ||
                !WritePod(file, mesh.boundsMax) ||
                !WritePod(file, hasMaterial))
            {
                return false;
            }

            if (vertexCount > 0)
            {
                file.write(
                    reinterpret_cast<const char*>(mesh.vertices.data()),
                    static_cast<std::streamsize>(mesh.vertices.size() * sizeof(Vertex)));
            }
            if (indexCount > 0)
            {
                file.write(
                    reinterpret_cast<const char*>(mesh.indices.data()),
                    static_cast<std::streamsize>(mesh.indices.size() * sizeof(std::uint32_t)));
            }
            if (!file)
                return false;

            if (material)
            {
                const std::uint8_t useTexture = material->useTexture ? 1u : 0u;
                if (!WriteString(file, material->shaderAsset) ||
                    !WriteString(file, material->textureAsset) ||
                    !WritePod(file, material->baseColor) ||
                    !WritePod(file, useTexture))
                {
                    return false;
                }
            }

            return static_cast<bool>(file);
        }

        bool ReadMeshBinaryCache(const std::filesystem::path& path, MeshData& outMesh, MaterialAsset* outMaterial)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return false;

            std::uint32_t magic = 0;
            std::uint32_t version = 0;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint8_t hasMaterial = 0;
            if (!ReadPod(file, magic) ||
                !ReadPod(file, version) ||
                magic != kMeshCacheMagic ||
                version != kMeshCacheVersion ||
                !ReadPod(file, vertexCount) ||
                !ReadPod(file, indexCount) ||
                !ReadPod(file, outMesh.boundsMin) ||
                !ReadPod(file, outMesh.boundsMax) ||
                !ReadPod(file, hasMaterial))
            {
                return false;
            }

            outMesh.vertices.resize(vertexCount);
            outMesh.indices.resize(indexCount);
            if (vertexCount > 0)
            {
                file.read(
                    reinterpret_cast<char*>(outMesh.vertices.data()),
                    static_cast<std::streamsize>(outMesh.vertices.size() * sizeof(Vertex)));
            }
            if (indexCount > 0)
            {
                file.read(
                    reinterpret_cast<char*>(outMesh.indices.data()),
                    static_cast<std::streamsize>(outMesh.indices.size() * sizeof(std::uint32_t)));
            }
            if (!file)
                return false;

            if (hasMaterial != 0 && outMaterial)
            {
                std::uint8_t useTexture = 0;
                if (!ReadString(file, outMaterial->shaderAsset) ||
                    !ReadString(file, outMaterial->textureAsset) ||
                    !ReadPod(file, outMaterial->baseColor) ||
                    !ReadPod(file, useTexture))
                {
                    return false;
                }
                outMaterial->useTexture = useTexture != 0;
            }

            return true;
        }

        bool WriteTextureBinaryCache(const std::filesystem::path& path, const TextureData& texture)
        {
            if (const auto parent = path.parent_path(); !parent.empty())
                std::filesystem::create_directories(parent);

            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file)
                return false;

            const std::uint32_t pixelCount = static_cast<std::uint32_t>(texture.pixels.size());
            if (!WritePod(file, kTextureCacheMagic) ||
                !WritePod(file, kTextureCacheVersion) ||
                !WritePod(file, texture.width) ||
                !WritePod(file, texture.height) ||
                !WritePod(file, texture.channels) ||
                !WritePod(file, pixelCount))
            {
                return false;
            }

            if (pixelCount > 0)
                file.write(reinterpret_cast<const char*>(texture.pixels.data()), pixelCount);
            return static_cast<bool>(file);
        }

        bool ReadTextureBinaryCache(const std::filesystem::path& path, TextureData& outTexture)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return false;

            std::uint32_t magic = 0;
            std::uint32_t version = 0;
            std::uint32_t pixelCount = 0;
            if (!ReadPod(file, magic) ||
                !ReadPod(file, version) ||
                magic != kTextureCacheMagic ||
                version != kTextureCacheVersion ||
                !ReadPod(file, outTexture.width) ||
                !ReadPod(file, outTexture.height) ||
                !ReadPod(file, outTexture.channels) ||
                !ReadPod(file, pixelCount))
            {
                return false;
            }

            outTexture.pixels.resize(pixelCount);
            if (pixelCount > 0)
                file.read(reinterpret_cast<char*>(outTexture.pixels.data()), pixelCount);
            return static_cast<bool>(file);
        }

        TextureData LoadTextureDataFromSource(const std::filesystem::path& path, std::string& outError)
        {
            TextureData texture{};
            int width = 0;
            int height = 0;
            int channels = 0;

            stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (!pixels)
            {
                outError = stbi_failure_reason() ? stbi_failure_reason() : "stb_image failed";
                return texture;
            }

            texture.width = width;
            texture.height = height;
            texture.channels = 4;
            texture.pixels.assign(
                pixels,
                pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
            stbi_image_free(pixels);
            return texture;
        }

        MeshData CreatePlaceholderMeshData()
        {
            MeshData mesh{};
            mesh.vertices = {
                { { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
                { { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
                { { 0.5f, 0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
                { { -0.5f, 0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } }
            };
            mesh.indices = { 0, 1, 2, 0, 2, 3 };
            mesh.boundsMin = { -0.5f, -0.5f, 0.0f };
            mesh.boundsMax = { 0.5f, 0.5f, 0.0f };
            return mesh;
        }

        MeshData CreateUvSphereMeshData(int slices, int stacks)
        {
            MeshData mesh{};
            slices = std::max(8, slices);
            stacks = std::max(4, stacks);

            for (int stack = 0; stack <= stacks; ++stack)
            {
                const float v = static_cast<float>(stack) / static_cast<float>(stacks);
                const float phi = 3.14159265359f * v;
                const float y = std::cos(phi) * 0.5f;
                const float ringRadius = std::sin(phi) * 0.5f;

                for (int slice = 0; slice <= slices; ++slice)
                {
                    const float u = static_cast<float>(slice) / static_cast<float>(slices);
                    const float theta = 6.28318530718f * u;
                    const float x = std::cos(theta) * ringRadius;
                    const float z = std::sin(theta) * ringRadius;

                    const Vec3 position{ x, y, z };
                    mesh.vertices.push_back({
                        position,
                        Normalize(position),
                        { u, 1.0f - v }
                    });
                }
            }

            const int rowStride = slices + 1;
            for (int stack = 0; stack < stacks; ++stack)
            {
                for (int slice = 0; slice < slices; ++slice)
                {
                    const std::uint32_t i0 = static_cast<std::uint32_t>(stack * rowStride + slice);
                    const std::uint32_t i1 = i0 + 1;
                    const std::uint32_t i2 = i0 + static_cast<std::uint32_t>(rowStride);
                    const std::uint32_t i3 = i2 + 1;

                    mesh.indices.push_back(i0);
                    mesh.indices.push_back(i2);
                    mesh.indices.push_back(i1);

                    mesh.indices.push_back(i1);
                    mesh.indices.push_back(i2);
                    mesh.indices.push_back(i3);
                }
            }

            mesh.boundsMin = { -0.5f, -0.5f, -0.5f };
            mesh.boundsMax = { 0.5f, 0.5f, 0.5f };
            return mesh;
        }

        TextureData CreatePlaceholderTextureData()
        {
            TextureData texture{};
            texture.width = 2;
            texture.height = 2;
            texture.channels = 4;
            texture.pixels = {
                255, 0, 255, 255, 16, 16, 16, 255,
                16, 16, 16, 255, 255, 0, 255, 255
            };
            return texture;
        }

        MaterialAsset ExtractMaterialFromAssimp(
            const aiScene& scene,
            int materialIndex,
            const std::filesystem::path& sourcePath,
            bool& outHasMaterial)
        {
            MaterialAsset material{};
            material.sourcePath = sourcePath;
            material.shaderAsset = kDefaultShaderAsset;
            material.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
            material.useTexture = false;
            material.hotReloadEnabled = true;
            outHasMaterial = false;

            if (materialIndex < 0 || materialIndex >= static_cast<int>(scene.mNumMaterials) || scene.mMaterials == nullptr)
                return material;

            const aiMaterial& aiMaterialRef = *scene.mMaterials[materialIndex];
            outHasMaterial = true;

            aiString texturePath{};
            if (aiMaterialRef.GetTexture(aiTextureType_BASE_COLOR, 0, &texturePath) == AI_SUCCESS ||
                aiMaterialRef.GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) == AI_SUCCESS)
            {
                const std::filesystem::path resolvedTexture = ResolveRelativeTo(sourcePath, texturePath.C_Str());
                material.textureAsset = MakeAssetReference(resolvedTexture);
                material.useTexture = !material.textureAsset.empty();
            }

            aiColor4D color{};
            if (aiGetMaterialColor(&aiMaterialRef, AI_MATKEY_COLOR_DIFFUSE, &color) == AI_SUCCESS)
                material.baseColor = { color.r, color.g, color.b, color.a };

            return material;
        }
    }

    namespace
    {
        ResourceManager::PendingMeshLoad LoadMeshJob(
            const std::string& key,
            const std::filesystem::path& sourcePath,
            bool allowBinaryCache)
        {
            ResourceManager::PendingMeshLoad result{};
            result.key = key;
            result.sourcePath = std::filesystem::weakly_canonical(sourcePath);
            result.binaryCachePath = MakeBinaryCachePath(key, ".meshbin");
            if (std::filesystem::exists(result.sourcePath))
                result.sourceWriteTime = std::filesystem::last_write_time(result.sourcePath);

            if (allowBinaryCache &&
                std::filesystem::exists(result.binaryCachePath) &&
                (!std::filesystem::exists(result.sourcePath) ||
                 std::filesystem::last_write_time(result.binaryCachePath) >= result.sourceWriteTime))
            {
                if (ReadMeshBinaryCache(result.binaryCachePath, result.mesh, &result.importedMaterial))
                {
                    result.hasImportedMaterial =
                        !result.importedMaterial.textureAsset.empty() ||
                        result.importedMaterial.useTexture;
                    result.loadedFromBinaryCache = true;
                    result.success = true;
                    return result;
                }
            }

            Assimp::Importer importer{};
            const unsigned int flags =
                aiProcess_Triangulate |
                aiProcess_JoinIdenticalVertices |
                aiProcess_GenSmoothNormals |
                aiProcess_ImproveCacheLocality |
                aiProcess_SortByPType;
            const aiScene* scene = importer.ReadFile(result.sourcePath.string(), flags);
            if (!scene || !scene->mRootNode)
            {
                result.error = importer.GetErrorString();
                return result;
            }

            Vec3 boundsMin{};
            Vec3 boundsMax{};
            bool boundsInitialized = false;
            int firstMaterialIndex = -1;
            AppendAssimpNodeMeshes(
                *scene,
                *scene->mRootNode,
                aiMatrix4x4{},
                result.mesh,
                boundsMin,
                boundsMax,
                boundsInitialized,
                firstMaterialIndex);

            if (result.mesh.vertices.empty())
            {
                result.error = "Assimp scene has no renderable vertices";
                return result;
            }

            result.mesh.boundsMin = boundsInitialized ? boundsMin : Vec3{ 0.0f, 0.0f, 0.0f };
            result.mesh.boundsMax = boundsInitialized ? boundsMax : Vec3{ 0.0f, 0.0f, 0.0f };
            result.importedMaterial = ExtractMaterialFromAssimp(*scene, firstMaterialIndex, result.sourcePath, result.hasImportedMaterial);
            result.success = true;

            if (!WriteMeshBinaryCache(
                    result.binaryCachePath,
                    result.mesh,
                    result.hasImportedMaterial ? &result.importedMaterial : nullptr))
            {
                Logger::Warn("Failed to write mesh cache: ", result.binaryCachePath.string());
            }

            return result;
        }

        ResourceManager::PendingTextureLoad LoadTextureJob(
            const std::string& key,
            const std::filesystem::path& sourcePath,
            bool allowBinaryCache)
        {
            ResourceManager::PendingTextureLoad result{};
            result.key = key;
            result.sourcePath = std::filesystem::weakly_canonical(sourcePath);
            result.binaryCachePath = MakeBinaryCachePath(key, ".texbin");
            if (std::filesystem::exists(result.sourcePath))
                result.sourceWriteTime = std::filesystem::last_write_time(result.sourcePath);

            if (allowBinaryCache &&
                std::filesystem::exists(result.binaryCachePath) &&
                (!std::filesystem::exists(result.sourcePath) ||
                 std::filesystem::last_write_time(result.binaryCachePath) >= result.sourceWriteTime))
            {
                if (ReadTextureBinaryCache(result.binaryCachePath, result.texture))
                {
                    result.loadedFromBinaryCache = true;
                    result.success = true;
                    return result;
                }
            }

            result.texture = LoadTextureDataFromSource(result.sourcePath, result.error);
            if (result.texture.width <= 0 || result.texture.height <= 0 || result.texture.pixels.empty())
                return result;

            result.success = true;
            if (!WriteTextureBinaryCache(result.binaryCachePath, result.texture))
                Logger::Warn("Failed to write texture cache: ", result.binaryCachePath.string());

            return result;
        }
    }

    void ResourceManager::SetRenderAdapter(IRenderAdapter* renderer)
    {
        m_renderer = renderer;
        EnsureBuiltInResources();
    }

    void ResourceManager::ForceReloadAll()
    {
        m_forceReloadAll = true;
    }

    void ResourceManager::ForceReloadShaders()
    {
        m_forceShaderReload = true;
    }

    void ResourceManager::UpdateHotReload()
    {
        FinalizeAsyncLoads();
        UpdateShaderHotReload();
        UpdateMaterialHotReload();
        UpdateMeshHotReload();
        UpdateTextureHotReload();
        m_forceReloadAll = false;
        m_forceShaderReload = false;
    }

    std::string ResourceManager::NormalizeAssetKey(const std::string& path) const
    {
        if (path.empty())
            return {};
        if (IsVirtualKey(path))
            return path;
        return ResolveAssetPath(path).lexically_normal().generic_string();
    }

    bool ResourceManager::IsVirtualKey(const std::string& path) const
    {
        return
            path.rfind("builtin:", 0) == 0 ||
            path.rfind("generated:", 0) == 0 ||
            path.rfind("memory:", 0) == 0;
    }

    ResourcePtr<MaterialAsset> ResourceManager::UpsertGeneratedMaterial(const std::string& key, const MaterialAsset& material)
    {
        auto& slot = m_materialCache[key];
        if (!slot)
            slot = std::make_shared<Resource<MaterialAsset>>();

        slot->key = key;
        slot->state = ResourceLoadState::Ready;
        slot->error.clear();
        slot->value = material;
        slot->value.placeholder = false;
        return slot;
    }

    ResourcePtr<MeshAsset> ResourceManager::CreatePlaceholderMeshResource()
    {
        if (!m_renderer)
            return nullptr;

        auto resource = std::make_shared<Resource<MeshAsset>>();
        resource->key = kPlaceholderMeshKey;
        resource->state = ResourceLoadState::Ready;
        resource->value.sourcePath = kPlaceholderMeshKey;
        resource->value.mesh = CreatePlaceholderMeshData();
        resource->value.gpuHandle = m_renderer->UploadMesh(resource->value.mesh);
        resource->value.placeholder = true;
        resource->value.hotReloadEnabled = false;
        if (resource->value.gpuHandle == InvalidMeshHandle)
        {
            resource->state = ResourceLoadState::Failed;
            resource->error = "Failed to upload placeholder mesh";
        }

        m_meshCache[resource->key] = resource;
        return resource;
    }

    ResourcePtr<MeshAsset> ResourceManager::CreateBuiltInSphereMeshResource()
    {
        if (!m_renderer)
            return nullptr;

        auto resource = std::make_shared<Resource<MeshAsset>>();
        resource->key = kBuiltInSphereMeshKey;
        resource->state = ResourceLoadState::Ready;
        resource->value.sourcePath = kBuiltInSphereMeshKey;
        resource->value.mesh = CreateUvSphereMeshData(24, 16);
        resource->value.gpuHandle = m_renderer->UploadMesh(resource->value.mesh);
        resource->value.placeholder = false;
        resource->value.hotReloadEnabled = false;
        if (resource->value.gpuHandle == InvalidMeshHandle)
        {
            resource->state = ResourceLoadState::Failed;
            resource->error = "Failed to upload built-in sphere mesh";
        }

        m_meshCache[resource->key] = resource;
        return resource;
    }

    ResourcePtr<TextureAsset> ResourceManager::CreatePlaceholderTextureResource()
    {
        if (!m_renderer)
            return nullptr;

        auto resource = std::make_shared<Resource<TextureAsset>>();
        resource->key = kPlaceholderTextureKey;
        resource->state = ResourceLoadState::Ready;
        resource->value.sourcePath = kPlaceholderTextureKey;
        const TextureData texture = CreatePlaceholderTextureData();
        resource->value.width = texture.width;
        resource->value.height = texture.height;
        resource->value.channels = texture.channels;
        resource->value.gpuHandle = m_renderer->CreateTexture(texture);
        resource->value.placeholder = true;
        resource->value.hotReloadEnabled = false;
        if (resource->value.gpuHandle == InvalidTextureHandle)
        {
            resource->state = ResourceLoadState::Failed;
            resource->error = "Failed to upload placeholder texture";
        }

        m_textureCache[resource->key] = resource;
        return resource;
    }

    ResourcePtr<MaterialAsset> ResourceManager::CreatePlaceholderMaterialResource()
    {
        auto resource = std::make_shared<Resource<MaterialAsset>>();
        resource->key = kPlaceholderMaterialKey;
        resource->state = ResourceLoadState::Ready;
        resource->value.sourcePath = kPlaceholderMaterialKey;
        resource->value.shaderAsset = kDefaultShaderAsset;
        resource->value.textureAsset = kPlaceholderTextureKey;
        resource->value.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        resource->value.useTexture = true;
        resource->value.placeholder = true;
        resource->value.hotReloadEnabled = false;
        m_materialCache[resource->key] = resource;
        return resource;
    }

    ResourcePtr<MaterialAsset> ResourceManager::CreateDefaultMaterialResource()
    {
        auto resource = std::make_shared<Resource<MaterialAsset>>();
        resource->key = kDefaultMaterialKey;
        resource->state = ResourceLoadState::Ready;
        resource->value.sourcePath = kDefaultMaterialKey;
        resource->value.shaderAsset = kDefaultShaderAsset;
        resource->value.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        resource->value.useTexture = false;
        resource->value.placeholder = false;
        resource->value.hotReloadEnabled = false;
        m_materialCache[resource->key] = resource;
        return resource;
    }

    void ResourceManager::EnsureBuiltInResources()
    {
        if (!m_defaultMaterial)
            m_defaultMaterial = CreateDefaultMaterialResource();
        if (!m_placeholderMaterial)
            m_placeholderMaterial = CreatePlaceholderMaterialResource();

        if (!m_renderer)
            return;

        if (!m_placeholderMesh)
            m_placeholderMesh = CreatePlaceholderMeshResource();
        if (!m_builtinSphereMesh)
            m_builtinSphereMesh = CreateBuiltInSphereMeshResource();
        if (!m_placeholderTexture)
            m_placeholderTexture = CreatePlaceholderTextureResource();
    }

    ResourcePtr<MeshAsset> ResourceManager::LoadMeshInternal(const std::string& path, bool forceReload)
    {
        EnsureBuiltInResources();
        if (!m_renderer)
        {
            Logger::Error("ResourceManager has no renderer while loading mesh: ", path);
            return nullptr;
        }

        const std::string key = NormalizeAssetKey(path);
        if (key.empty())
            return nullptr;

        if (!forceReload)
        {
            const auto it = m_meshCache.find(key);
            if (it != m_meshCache.end())
                return it->second;
        }

        if (const auto pending = m_pendingMeshLoads.find(key); pending != m_pendingMeshLoads.end())
        {
            PendingMeshLoad result = pending->second.get();
            m_pendingMeshLoads.erase(pending);
            auto resource = m_meshCache[key];
            if (!resource)
                resource = std::make_shared<Resource<MeshAsset>>();

            resource->key = key;
            if (!result.success)
            {
                resource->state = ResourceLoadState::Failed;
                resource->error = result.error;
                m_meshCache[key] = resource;
                Logger::Error("Mesh load failed: ", path, " | ", result.error);
                return resource;
            }

            resource->value.sourcePath = result.sourcePath;
            resource->value.binaryCachePath = result.binaryCachePath;
            resource->value.sourceWriteTime = result.sourceWriteTime;
            resource->value.mesh = result.mesh;
            resource->value.placeholder = false;
            resource->value.hotReloadEnabled = true;
            resource->value.gpuHandle = m_renderer->UploadMesh(result.mesh);

            if (result.hasImportedMaterial)
            {
                const std::string materialKey = "generated:material:" + MakeHexString(Fnv1a64(key));
                result.importedMaterial.sourcePath = result.sourcePath;
                result.importedMaterial.sourceWriteTime = result.sourceWriteTime;
                UpsertGeneratedMaterial(materialKey, result.importedMaterial);
                resource->value.importedMaterialAsset = materialKey;
            }
            else
            {
                resource->value.importedMaterialAsset.clear();
            }

            resource->state = resource->value.gpuHandle != InvalidMeshHandle
                ? ResourceLoadState::Ready
                : ResourceLoadState::Failed;
            resource->error = resource->state == ResourceLoadState::Ready ? std::string{} : "Failed to upload mesh to GPU";
            m_meshCache[key] = resource;
            return resource;
        }

        if (IsVirtualKey(key))
        {
            const auto it = m_meshCache.find(key);
            return it != m_meshCache.end() ? it->second : nullptr;
        }

        PendingMeshLoad result = LoadMeshJob(key, ResolveAssetPath(path), !forceReload);
        auto resource = m_meshCache[key];
        if (!resource)
            resource = std::make_shared<Resource<MeshAsset>>();

        resource->key = key;
        if (!result.success)
        {
            resource->state = ResourceLoadState::Failed;
            resource->error = result.error;
            m_meshCache[key] = resource;
            Logger::Error("Mesh load failed: ", path, " | ", result.error);
            return resource;
        }

        resource->value.sourcePath = result.sourcePath;
        resource->value.binaryCachePath = result.binaryCachePath;
        resource->value.sourceWriteTime = result.sourceWriteTime;
        resource->value.mesh = result.mesh;
        resource->value.placeholder = false;
        resource->value.hotReloadEnabled = true;

        const bool reloaded =
            forceReload &&
            resource->value.gpuHandle != InvalidMeshHandle &&
            !resource->value.placeholder &&
            (!m_placeholderMesh || resource->value.gpuHandle != m_placeholderMesh->value.gpuHandle) &&
            m_renderer->ReloadMesh(resource->value.gpuHandle, result.mesh);
        if (!reloaded)
            resource->value.gpuHandle = m_renderer->UploadMesh(result.mesh);

        if (result.hasImportedMaterial)
        {
            const std::string materialKey = "generated:material:" + MakeHexString(Fnv1a64(key));
            result.importedMaterial.sourcePath = result.sourcePath;
            result.importedMaterial.sourceWriteTime = result.sourceWriteTime;
            UpsertGeneratedMaterial(materialKey, result.importedMaterial);
            resource->value.importedMaterialAsset = materialKey;
        }
        else
        {
            resource->value.importedMaterialAsset.clear();
        }

        resource->state = resource->value.gpuHandle != InvalidMeshHandle
            ? ResourceLoadState::Ready
            : ResourceLoadState::Failed;
        resource->error = resource->state == ResourceLoadState::Ready ? std::string{} : "Failed to upload mesh to GPU";
        m_meshCache[key] = resource;

        Logger::Info(
            result.loadedFromBinaryCache ? "Loaded mesh from binary cache: " : "Loaded mesh via Assimp: ",
            path);
        return resource;
    }

    ResourcePtr<TextureAsset> ResourceManager::LoadTextureInternal(const std::string& path, bool forceReload)
    {
        EnsureBuiltInResources();
        if (!m_renderer)
        {
            Logger::Error("ResourceManager has no renderer while loading texture: ", path);
            return nullptr;
        }

        const std::string key = NormalizeAssetKey(path);
        if (key.empty())
            return nullptr;

        if (!forceReload)
        {
            const auto it = m_textureCache.find(key);
            if (it != m_textureCache.end())
                return it->second;
        }

        if (const auto pending = m_pendingTextureLoads.find(key); pending != m_pendingTextureLoads.end())
        {
            PendingTextureLoad result = pending->second.get();
            m_pendingTextureLoads.erase(pending);
            auto resource = m_textureCache[key];
            if (!resource)
                resource = std::make_shared<Resource<TextureAsset>>();

            resource->key = key;
            if (!result.success)
            {
                resource->state = ResourceLoadState::Failed;
                resource->error = result.error;
                m_textureCache[key] = resource;
                Logger::Error("Texture load failed: ", path, " | ", result.error);
                return resource;
            }

            resource->value.sourcePath = result.sourcePath;
            resource->value.binaryCachePath = result.binaryCachePath;
            resource->value.sourceWriteTime = result.sourceWriteTime;
            resource->value.width = result.texture.width;
            resource->value.height = result.texture.height;
            resource->value.channels = result.texture.channels;
            resource->value.placeholder = false;
            resource->value.hotReloadEnabled = true;
            resource->value.gpuHandle = m_renderer->CreateTexture(result.texture);

            resource->state = resource->value.gpuHandle != InvalidTextureHandle
                ? ResourceLoadState::Ready
                : ResourceLoadState::Failed;
            resource->error = resource->state == ResourceLoadState::Ready ? std::string{} : "Failed to upload texture to GPU";
            m_textureCache[key] = resource;
            return resource;
        }

        if (IsVirtualKey(key))
        {
            const auto it = m_textureCache.find(key);
            return it != m_textureCache.end() ? it->second : nullptr;
        }

        PendingTextureLoad result = LoadTextureJob(key, ResolveAssetPath(path), !forceReload);
        auto resource = m_textureCache[key];
        if (!resource)
            resource = std::make_shared<Resource<TextureAsset>>();

        resource->key = key;
        if (!result.success)
        {
            resource->state = ResourceLoadState::Failed;
            resource->error = result.error;
            m_textureCache[key] = resource;
            Logger::Error("Texture load failed: ", path, " | ", result.error);
            return resource;
        }

        resource->value.sourcePath = result.sourcePath;
        resource->value.binaryCachePath = result.binaryCachePath;
        resource->value.sourceWriteTime = result.sourceWriteTime;
        resource->value.width = result.texture.width;
        resource->value.height = result.texture.height;
        resource->value.channels = result.texture.channels;
        resource->value.placeholder = false;
        resource->value.hotReloadEnabled = true;

        const bool reloaded =
            forceReload &&
            resource->value.gpuHandle != InvalidTextureHandle &&
            !resource->value.placeholder &&
            (!m_placeholderTexture || resource->value.gpuHandle != m_placeholderTexture->value.gpuHandle) &&
            m_renderer->ReloadTexture(resource->value.gpuHandle, result.texture);
        if (!reloaded)
            resource->value.gpuHandle = m_renderer->CreateTexture(result.texture);

        resource->state = resource->value.gpuHandle != InvalidTextureHandle
            ? ResourceLoadState::Ready
            : ResourceLoadState::Failed;
        resource->error = resource->state == ResourceLoadState::Ready ? std::string{} : "Failed to upload texture to GPU";
        m_textureCache[key] = resource;

        Logger::Info(
            result.loadedFromBinaryCache ? "Loaded texture from binary cache: " : "Loaded texture: ",
            path);
        return resource;
    }

    ResourcePtr<ShaderAsset> ResourceManager::LoadShaderInternal(const std::string& path, bool forceReload)
    {
        if (!m_renderer)
        {
            Logger::Error("ResourceManager has no renderer while loading shader: ", path);
            return nullptr;
        }

        const std::string key = NormalizeAssetKey(path);
        if (key.empty())
            return nullptr;

        if (!forceReload)
        {
            const auto it = m_shaderCache.find(key);
            if (it != m_shaderCache.end())
                return it->second;
        }

        const std::filesystem::path manifestPath = ResolveAssetPath(path);
        std::ifstream file(manifestPath, std::ios::binary);
        if (!file)
        {
            Logger::Error("Failed to open shader manifest: ", manifestPath.string());
            return nullptr;
        }

        json manifest{};
        try
        {
            file >> manifest;
        }
        catch (const std::exception& ex)
        {
            Logger::Error("Failed to parse shader manifest: ", manifestPath.string(), " | ", ex.what());
            return nullptr;
        }

        const std::string vertexRelative = manifest.value("vertex", std::string{});
        const std::string fragmentRelative = manifest.value("fragment", std::string{});
        if (vertexRelative.empty() || fragmentRelative.empty())
        {
            Logger::Error("Shader manifest is incomplete: ", manifestPath.string());
            return nullptr;
        }

        ShaderSource shaderSource{};
        shaderSource.vertexPath = ResolveRelativeTo(manifestPath, vertexRelative);
        shaderSource.fragmentPath = ResolveRelativeTo(manifestPath, fragmentRelative);
        shaderSource.vertexCode = ReadTextFile(shaderSource.vertexPath);
        shaderSource.fragmentCode = ReadTextFile(shaderSource.fragmentPath);
        shaderSource.hotReloadEnabled = manifest.value("hotReload", false);
        if (shaderSource.vertexCode.empty() || shaderSource.fragmentCode.empty())
        {
            Logger::Error("Failed to read shader source files for manifest: ", manifestPath.string());
            return nullptr;
        }

        auto& slot = m_shaderCache[key];
        if (!slot)
            slot = std::make_shared<Resource<ShaderAsset>>();

        slot->key = key;
        slot->error.clear();

        if (forceReload && slot->value.gpuHandle != InvalidShaderHandle)
        {
            if (!m_renderer->ReloadShaderProgram(slot->value.gpuHandle, shaderSource))
            {
                slot->state = ResourceLoadState::Failed;
                slot->error = "Failed to reload shader program";
                Logger::Error("Failed to reload shader: ", manifestPath.string());
                return slot;
            }
        }
        else
        {
            slot->value.gpuHandle = m_renderer->CreateShaderProgram(shaderSource);
            if (slot->value.gpuHandle == InvalidShaderHandle)
            {
                slot->state = ResourceLoadState::Failed;
                slot->error = "Failed to compile shader program";
                Logger::Error("Failed to create shader: ", manifestPath.string());
                return slot;
            }
        }

        slot->state = ResourceLoadState::Ready;
        slot->value.manifestPath = std::filesystem::weakly_canonical(manifestPath);
        slot->value.vertexPath = std::filesystem::weakly_canonical(shaderSource.vertexPath);
        slot->value.fragmentPath = std::filesystem::weakly_canonical(shaderSource.fragmentPath);
        slot->value.manifestWriteTime = std::filesystem::last_write_time(slot->value.manifestPath);
        slot->value.vertexWriteTime = std::filesystem::last_write_time(slot->value.vertexPath);
        slot->value.fragmentWriteTime = std::filesystem::last_write_time(slot->value.fragmentPath);
        slot->value.hotReloadEnabled = shaderSource.hotReloadEnabled;
        slot->value.placeholder = false;

        Logger::Info("Loaded shader manifest: ", path);
        return slot;
    }

    ResourcePtr<MaterialAsset> ResourceManager::LoadMaterialInternal(const std::string& path, bool forceReload)
    {
        EnsureBuiltInResources();

        const std::string key = NormalizeAssetKey(path);
        if (key.empty())
            return DefaultMaterial();

        if (!forceReload)
        {
            const auto it = m_materialCache.find(key);
            if (it != m_materialCache.end())
                return it->second;
        }

        if (IsVirtualKey(key))
        {
            const auto it = m_materialCache.find(key);
            return it != m_materialCache.end() ? it->second : DefaultMaterial();
        }

        const std::filesystem::path materialPath = ResolveAssetPath(path);
        std::ifstream file(materialPath, std::ios::binary);
        if (!file)
        {
            Logger::Error("Failed to open material file: ", materialPath.string());
            return PlaceholderMaterial();
        }

        json document{};
        try
        {
            file >> document;
        }
        catch (const std::exception& ex)
        {
            Logger::Error("Failed to parse material JSON: ", materialPath.string(), " | ", ex.what());
            return PlaceholderMaterial();
        }

        auto& slot = m_materialCache[key];
        if (!slot)
            slot = std::make_shared<Resource<MaterialAsset>>();

        slot->key = key;
        slot->state = ResourceLoadState::Ready;
        slot->error.clear();
        slot->value.sourcePath = std::filesystem::weakly_canonical(materialPath);
        slot->value.sourceWriteTime = std::filesystem::exists(slot->value.sourcePath)
            ? std::filesystem::last_write_time(slot->value.sourcePath)
            : std::filesystem::file_time_type{};
        slot->value.shaderAsset = document.value("shaderAsset", std::string{ kDefaultShaderAsset });
        slot->value.textureAsset = document.value("textureAsset", std::string{});
        slot->value.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        if (document.contains("baseColor") && !ReadVec4(document["baseColor"], slot->value.baseColor))
        {
            slot->state = ResourceLoadState::Failed;
            slot->error = "Material baseColor is invalid";
            Logger::Error("Invalid material baseColor: ", materialPath.string());
            return slot;
        }

        slot->value.useTexture = document.value("useTexture", !slot->value.textureAsset.empty());
        slot->value.hotReloadEnabled = document.value("hotReload", true);
        slot->value.placeholder = false;

        Logger::Info("Loaded material: ", path);
        return slot;
    }

    ResourcePtr<MeshAsset> ResourceManager::LoadMeshAsync(const std::string& path)
    {
        EnsureBuiltInResources();
        if (!m_renderer)
            return nullptr;

        const std::string key = NormalizeAssetKey(path);
        if (key.empty())
            return nullptr;
        if (IsVirtualKey(key))
            return LoadMeshInternal(path, false);

        auto& slot = m_meshCache[key];
        if (slot && (slot->IsReady() || slot->IsLoading()))
            return slot;
        if (!slot)
            slot = std::make_shared<Resource<MeshAsset>>();

        slot->key = key;
        slot->state = ResourceLoadState::Loading;
        slot->error.clear();
        if (m_placeholderMesh)
            slot->value = m_placeholderMesh->value;
        slot->value.sourcePath = ResolveAssetPath(path);
        slot->value.binaryCachePath = MakeBinaryCachePath(key, ".meshbin");
        slot->value.placeholder = true;

        if (m_pendingMeshLoads.find(key) == m_pendingMeshLoads.end())
        {
            const std::filesystem::path sourcePath = ResolveAssetPath(path);
            m_pendingMeshLoads.emplace(
                key,
                std::async(std::launch::async, [key, sourcePath]() {
                    return LoadMeshJob(key, sourcePath, true);
                }));
        }

        return slot;
    }

    ResourcePtr<TextureAsset> ResourceManager::LoadTextureAsync(const std::string& path)
    {
        EnsureBuiltInResources();
        if (!m_renderer)
            return nullptr;

        const std::string key = NormalizeAssetKey(path);
        if (key.empty())
            return nullptr;
        if (IsVirtualKey(key))
            return LoadTextureInternal(path, false);

        auto& slot = m_textureCache[key];
        if (slot && (slot->IsReady() || slot->IsLoading()))
            return slot;
        if (!slot)
            slot = std::make_shared<Resource<TextureAsset>>();

        slot->key = key;
        slot->state = ResourceLoadState::Loading;
        slot->error.clear();
        if (m_placeholderTexture)
            slot->value = m_placeholderTexture->value;
        slot->value.sourcePath = ResolveAssetPath(path);
        slot->value.binaryCachePath = MakeBinaryCachePath(key, ".texbin");
        slot->value.placeholder = true;

        if (m_pendingTextureLoads.find(key) == m_pendingTextureLoads.end())
        {
            const std::filesystem::path sourcePath = ResolveAssetPath(path);
            m_pendingTextureLoads.emplace(
                key,
                std::async(std::launch::async, [key, sourcePath]() {
                    return LoadTextureJob(key, sourcePath, true);
                }));
        }

        return slot;
    }

    void ResourceManager::FinalizeAsyncLoads()
    {
        if (!m_renderer)
            return;

        for (auto it = m_pendingMeshLoads.begin(); it != m_pendingMeshLoads.end();)
        {
            if (!IsFutureReady(it->second))
            {
                ++it;
                continue;
            }

            PendingMeshLoad result = it->second.get();
            auto resource = m_meshCache[result.key];
            if (!resource)
                resource = std::make_shared<Resource<MeshAsset>>();

            resource->key = result.key;
            if (!result.success)
            {
                resource->state = ResourceLoadState::Failed;
                resource->error = result.error;
                Logger::Error("Async mesh load failed: ", result.sourcePath.string(), " | ", result.error);
            }
            else
            {
                resource->value.sourcePath = result.sourcePath;
                resource->value.binaryCachePath = result.binaryCachePath;
                resource->value.sourceWriteTime = result.sourceWriteTime;
                resource->value.mesh = result.mesh;
                resource->value.placeholder = false;
                resource->value.hotReloadEnabled = true;

                const bool reloaded =
                    resource->value.gpuHandle != InvalidMeshHandle &&
                    !resource->value.placeholder &&
                    (!m_placeholderMesh || resource->value.gpuHandle != m_placeholderMesh->value.gpuHandle) &&
                    m_renderer->ReloadMesh(resource->value.gpuHandle, result.mesh);
                if (!reloaded)
                    resource->value.gpuHandle = m_renderer->UploadMesh(result.mesh);

                if (result.hasImportedMaterial)
                {
                    const std::string materialKey = "generated:material:" + MakeHexString(Fnv1a64(result.key));
                    result.importedMaterial.sourcePath = result.sourcePath;
                    result.importedMaterial.sourceWriteTime = result.sourceWriteTime;
                    UpsertGeneratedMaterial(materialKey, result.importedMaterial);
                    resource->value.importedMaterialAsset = materialKey;
                }
                else
                {
                    resource->value.importedMaterialAsset.clear();
                }

                resource->state = resource->value.gpuHandle != InvalidMeshHandle
                    ? ResourceLoadState::Ready
                    : ResourceLoadState::Failed;
                resource->error = resource->state == ResourceLoadState::Ready ? std::string{} : "Failed to upload mesh to GPU";
                Logger::Info(
                    result.loadedFromBinaryCache ? "Async mesh finalized from binary cache: " : "Async mesh finalized: ",
                    result.sourcePath.string());
            }

            m_meshCache[result.key] = resource;
            it = m_pendingMeshLoads.erase(it);
        }

        for (auto it = m_pendingTextureLoads.begin(); it != m_pendingTextureLoads.end();)
        {
            if (!IsFutureReady(it->second))
            {
                ++it;
                continue;
            }

            PendingTextureLoad result = it->second.get();
            auto resource = m_textureCache[result.key];
            if (!resource)
                resource = std::make_shared<Resource<TextureAsset>>();

            resource->key = result.key;
            if (!result.success)
            {
                resource->state = ResourceLoadState::Failed;
                resource->error = result.error;
                Logger::Error("Async texture load failed: ", result.sourcePath.string(), " | ", result.error);
            }
            else
            {
                resource->value.sourcePath = result.sourcePath;
                resource->value.binaryCachePath = result.binaryCachePath;
                resource->value.sourceWriteTime = result.sourceWriteTime;
                resource->value.width = result.texture.width;
                resource->value.height = result.texture.height;
                resource->value.channels = result.texture.channels;
                resource->value.placeholder = false;
                resource->value.hotReloadEnabled = true;

                const bool reloaded =
                    resource->value.gpuHandle != InvalidTextureHandle &&
                    !resource->value.placeholder &&
                    (!m_placeholderTexture || resource->value.gpuHandle != m_placeholderTexture->value.gpuHandle) &&
                    m_renderer->ReloadTexture(resource->value.gpuHandle, result.texture);
                if (!reloaded)
                    resource->value.gpuHandle = m_renderer->CreateTexture(result.texture);

                resource->state = resource->value.gpuHandle != InvalidTextureHandle
                    ? ResourceLoadState::Ready
                    : ResourceLoadState::Failed;
                resource->error = resource->state == ResourceLoadState::Ready ? std::string{} : "Failed to upload texture to GPU";
                Logger::Info(
                    result.loadedFromBinaryCache ? "Async texture finalized from binary cache: " : "Async texture finalized: ",
                    result.sourcePath.string());
            }

            m_textureCache[result.key] = resource;
            it = m_pendingTextureLoads.erase(it);
        }
    }

    void ResourceManager::UpdateShaderHotReload()
    {
        const bool forceReload = m_forceReloadAll || m_forceShaderReload;

        for (const auto& [key, resource] : m_shaderCache)
        {
            if (!resource || !resource->IsReady())
                continue;
            if (!forceReload && !resource->value.hotReloadEnabled)
                continue;

            bool changed = forceReload;
            if (!changed && !resource->value.manifestPath.empty() && std::filesystem::exists(resource->value.manifestPath))
                changed = std::filesystem::last_write_time(resource->value.manifestPath) != resource->value.manifestWriteTime;
            if (!changed && !resource->value.vertexPath.empty() && std::filesystem::exists(resource->value.vertexPath))
                changed = std::filesystem::last_write_time(resource->value.vertexPath) != resource->value.vertexWriteTime;
            if (!changed && !resource->value.fragmentPath.empty() && std::filesystem::exists(resource->value.fragmentPath))
                changed = std::filesystem::last_write_time(resource->value.fragmentPath) != resource->value.fragmentWriteTime;

            if (changed)
                (void)LoadShaderInternal(key, true);
        }
    }

    void ResourceManager::UpdateMaterialHotReload()
    {
        const bool forceReload = m_forceReloadAll;

        for (const auto& [key, resource] : m_materialCache)
        {
            if (!resource || !resource->IsReady())
                continue;
            if (IsVirtualKey(key))
                continue;
            if (!forceReload && !resource->value.hotReloadEnabled)
                continue;
            if (resource->value.sourcePath.empty() || !std::filesystem::exists(resource->value.sourcePath))
                continue;

            const bool changed =
                forceReload ||
                std::filesystem::last_write_time(resource->value.sourcePath) != resource->value.sourceWriteTime;
            if (changed)
                (void)LoadMaterialInternal(key, true);
        }
    }

    void ResourceManager::UpdateMeshHotReload()
    {
        const bool forceReload = m_forceReloadAll;

        for (const auto& [key, resource] : m_meshCache)
        {
            if (!resource || !resource->IsReady())
                continue;
            if (IsVirtualKey(key))
                continue;
            if (!forceReload && !resource->value.hotReloadEnabled)
                continue;
            if (resource->value.sourcePath.empty() || !std::filesystem::exists(resource->value.sourcePath))
                continue;

            const bool changed =
                forceReload ||
                std::filesystem::last_write_time(resource->value.sourcePath) != resource->value.sourceWriteTime;
            if (changed)
                (void)LoadMeshInternal(key, true);
        }
    }

    void ResourceManager::UpdateTextureHotReload()
    {
        const bool forceReload = m_forceReloadAll;

        for (const auto& [key, resource] : m_textureCache)
        {
            if (!resource || !resource->IsReady())
                continue;
            if (IsVirtualKey(key))
                continue;
            if (!forceReload && !resource->value.hotReloadEnabled)
                continue;
            if (resource->value.sourcePath.empty() || !std::filesystem::exists(resource->value.sourcePath))
                continue;

            const bool changed =
                forceReload ||
                std::filesystem::last_write_time(resource->value.sourcePath) != resource->value.sourceWriteTime;
            if (changed)
                (void)LoadTextureInternal(key, true);
        }
    }
}
