#include "SceneSerializer.h"

#include "ECSComponents.h"
#include "Logger.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <optional>
#include <set>
#include <vector>

namespace archi
{
    namespace
    {
        using json = nlohmann::json;

        struct SerializedEntityData
        {
            EntityId id = InvalidEntityId;
            std::optional<Tag> tag{};
            std::optional<Transform> transform{};
            std::optional<MeshRenderer> meshRenderer{};
            std::optional<Hierarchy> hierarchy{};
            std::optional<SpinAnimation> spinAnimation{};
            std::optional<Camera> camera{};
            std::optional<CameraController> cameraController{};
        };

        json ToJson(const Vec3& value)
        {
            return json::array({ value.x, value.y, value.z });
        }

        json ToJson(const Vec4& value)
        {
            return json::array({ value.x, value.y, value.z, value.w });
        }

        bool ReadVec3(const json& value, Vec3& outValue)
        {
            if (!value.is_array() || value.size() != 3)
                return false;

            outValue = {
                value[0].get<float>(),
                value[1].get<float>(),
                value[2].get<float>()
            };
            return true;
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

        std::string ReadOptionalString(const json& objectValue, const char* key, const std::string& defaultValue = {})
        {
            if (!objectValue.contains(key) || objectValue[key].is_null())
                return defaultValue;
            if (!objectValue[key].is_string())
                return defaultValue;
            return objectValue[key].get<std::string>();
        }

        std::string LegacyPrimitiveToMeshAsset(const std::string& primitive)
        {
            if (primitive == "Line")
                return "models/cheburashka.obj";
            if (primitive == "Triangle")
                return "models/pyramid.obj";
            if (primitive == "Quad")
                return "models/plane.obj";
            if (primitive == "Cube")
                return "models/negr.obj";
            return "models/negr.obj";
        }

        bool ParseEntity(const json& entityValue, SerializedEntityData& outEntity, std::string& outError)
        {
            if (!entityValue.is_object())
            {
                outError = "Entity must be an object";
                return false;
            }

            if (!entityValue.contains("id") || !entityValue["id"].is_number_unsigned())
            {
                outError = "Entity id is missing or invalid";
                return false;
            }

            outEntity.id = entityValue["id"].get<EntityId>();
            if (outEntity.id == InvalidEntityId)
            {
                outError = "Entity id must be non-zero";
                return false;
            }

            if (!entityValue.contains("components") || !entityValue["components"].is_object())
            {
                outError = "Entity components object is missing";
                return false;
            }

            const json& components = entityValue["components"];

            if (components.contains("Tag"))
            {
                Tag tag{};
                tag.name = components["Tag"].value("name", std::string{});
                outEntity.tag = tag;
            }

            if (components.contains("Transform"))
            {
                Transform transform{};
                const json& transformValue = components["Transform"];
                if (!ReadVec3(transformValue.at("position"), transform.position) ||
                    !ReadVec3(transformValue.at("rotation"), transform.rotation) ||
                    !ReadVec3(transformValue.at("scale"), transform.scale))
                {
                    outError = "Transform component is invalid";
                    return false;
                }
                outEntity.transform = transform;
            }

            if (components.contains("MeshRenderer"))
            {
                MeshRenderer mesh{};
                const json& meshValue = components["MeshRenderer"];

                if (meshValue.contains("meshAsset"))
                {
                    mesh.meshAsset = ReadOptionalString(meshValue, "meshAsset");
                    mesh.materialAsset = ReadOptionalString(meshValue, "materialAsset");
                    mesh.textureAsset = ReadOptionalString(meshValue, "textureAsset");
                    mesh.shaderAsset = ReadOptionalString(meshValue, "shaderAsset");
                    mesh.asyncLoad = meshValue.value("asyncLoad", true);
                    if (meshValue.contains("tintColor") && !ReadVec4(meshValue["tintColor"], mesh.tintColor))
                    {
                        outError = "MeshRenderer.tintColor is invalid";
                        return false;
                    }
                }
                else
                {
                    const std::string primitive = ReadOptionalString(meshValue, "primitive", "Cube");
                    mesh.meshAsset = LegacyPrimitiveToMeshAsset(primitive);
                    mesh.textureAsset = ReadOptionalString(meshValue, "texturePath");
                    mesh.shaderAsset = "shaders/textured.shader.json";
                    mesh.asyncLoad = true;
                    if (meshValue.contains("color") && !ReadVec4(meshValue["color"], mesh.tintColor))
                    {
                        outError = "Legacy MeshRenderer.color is invalid";
                        return false;
                    }
                }

                if (mesh.meshAsset.empty())
                {
                    outError = "MeshRenderer resource references are incomplete";
                    return false;
                }

                outEntity.meshRenderer = mesh;
            }

            if (components.contains("Hierarchy"))
            {
                Hierarchy hierarchy{};
                const json& hierarchyValue = components["Hierarchy"];
                if (hierarchyValue.contains("parent"))
                    hierarchy.parent = Entity{ hierarchyValue["parent"].get<EntityId>() };

                if (hierarchyValue.contains("children") && hierarchyValue["children"].is_array())
                {
                    for (const auto& childValue : hierarchyValue["children"])
                        hierarchy.children.push_back(Entity{ childValue.get<EntityId>() });
                }
                outEntity.hierarchy = hierarchy;
            }

            if (components.contains("SpinAnimation"))
            {
                SpinAnimation animation{};
                const json& value = components["SpinAnimation"];
                if (!ReadVec3(value.at("angularVelocity"), animation.angularVelocity) ||
                    !ReadVec3(value.at("translationAxis"), animation.translationAxis) ||
                    !ReadVec3(value.at("anchorPosition"), animation.anchorPosition))
                {
                    outError = "SpinAnimation vectors are invalid";
                    return false;
                }

                animation.translationAmplitude = value.value("translationAmplitude", 0.0f);
                animation.translationSpeed = value.value("translationSpeed", 0.0f);
                animation.elapsed = value.value("elapsed", 0.0);
                outEntity.spinAnimation = animation;
            }

            if (components.contains("Camera"))
            {
                Camera camera{};
                const json& value = components["Camera"];
                camera.isPrimary = value.value("isPrimary", true);
                camera.orthographicHalfHeight = value.value("orthographicHalfHeight", 1.25f);
                camera.nearPlane = value.value("nearPlane", -20.0f);
                camera.farPlane = value.value("farPlane", 20.0f);
                outEntity.camera = camera;
            }

            if (components.contains("CameraController"))
            {
                CameraController controller{};
                const json& value = components["CameraController"];
                controller.moveSpeed = value.value("moveSpeed", 2.2f);
                controller.zoomSpeed = value.value("zoomSpeed", 1.2f);
                outEntity.cameraController = controller;
            }

            return true;
        }
    }

    bool SceneSerializer::SaveWorld(const World& world, const std::filesystem::path& path)
    {
        json root{};
        root["version"] = 4;
        root["entities"] = json::array();

        world.ForEachEntity([&](Entity entity) {
            json entityValue{};
            entityValue["id"] = entity.id;
            entityValue["components"] = json::object();
            json& components = entityValue["components"];

            if (const auto* tag = world.GetComponent<Tag>(entity))
                components["Tag"] = { { "name", tag->name } };

            if (const auto* transform = world.GetComponent<Transform>(entity))
            {
                components["Transform"] = {
                    { "position", ToJson(transform->position) },
                    { "rotation", ToJson(transform->rotation) },
                    { "scale", ToJson(transform->scale) }
                };
            }

            if (const auto* mesh = world.GetComponent<MeshRenderer>(entity))
            {
                components["MeshRenderer"] = {
                    { "meshAsset", mesh->meshAsset },
                    { "materialAsset", mesh->materialAsset },
                    { "textureAsset", mesh->textureAsset },
                    { "shaderAsset", mesh->shaderAsset },
                    { "tintColor", ToJson(mesh->tintColor) },
                    { "asyncLoad", mesh->asyncLoad }
                };
            }

            if (const auto* hierarchy = world.GetComponent<Hierarchy>(entity))
            {
                json children = json::array();
                for (const Entity child : hierarchy->children)
                    children.push_back(child.id);

                components["Hierarchy"] = {
                    { "parent", hierarchy->parent.id },
                    { "children", std::move(children) }
                };
            }

            if (const auto* animation = world.GetComponent<SpinAnimation>(entity))
            {
                components["SpinAnimation"] = {
                    { "angularVelocity", ToJson(animation->angularVelocity) },
                    { "translationAxis", ToJson(animation->translationAxis) },
                    { "translationAmplitude", animation->translationAmplitude },
                    { "translationSpeed", animation->translationSpeed },
                    { "anchorPosition", ToJson(animation->anchorPosition) },
                    { "elapsed", animation->elapsed }
                };
            }

            if (const auto* camera = world.GetComponent<Camera>(entity))
            {
                components["Camera"] = {
                    { "isPrimary", camera->isPrimary },
                    { "orthographicHalfHeight", camera->orthographicHalfHeight },
                    { "nearPlane", camera->nearPlane },
                    { "farPlane", camera->farPlane }
                };
            }

            if (const auto* controller = world.GetComponent<CameraController>(entity))
            {
                components["CameraController"] = {
                    { "moveSpeed", controller->moveSpeed },
                    { "zoomSpeed", controller->zoomSpeed }
                };
            }

            root["entities"].push_back(std::move(entityValue));
        });

        if (const auto parentPath = path.parent_path(); !parentPath.empty())
            std::filesystem::create_directories(parentPath);

        std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file)
        {
            Logger::Error("Failed to open scene file for writing: ", path.string());
            return false;
        }

        file << root.dump(2);
        Logger::Info("Scene saved to: ", path.string());
        return true;
    }

    bool SceneSerializer::LoadWorld(World& world, const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file)
        {
            Logger::Warn("Scene file not found: ", path.string());
            return false;
        }

        json root{};
        try
        {
            file >> root;
        }
        catch (const std::exception& ex)
        {
            Logger::Error("Failed to parse scene JSON: ", ex.what());
            return false;
        }

        if (!root.contains("entities") || !root["entities"].is_array())
        {
            Logger::Error("Scene JSON does not contain an entities array");
            return false;
        }

        std::vector<SerializedEntityData> serializedEntities{};
        std::set<EntityId> uniqueIds{};

        for (const auto& entityValue : root["entities"])
        {
            SerializedEntityData entityData{};
            std::string error{};
            if (!ParseEntity(entityValue, entityData, error))
            {
                Logger::Error("Failed to parse entity from scene: ", error);
                return false;
            }

            if (!uniqueIds.insert(entityData.id).second)
            {
                Logger::Error("Duplicate entity id in scene: ", entityData.id);
                return false;
            }

            serializedEntities.push_back(std::move(entityData));
        }

        world.ClearEntities();

        for (const auto& entityData : serializedEntities)
        {
            const Entity created = world.CreateEntityWithId(entityData.id);
            if (!created)
            {
                Logger::Error("Failed to create entity from scene with id: ", entityData.id);
                world.ClearEntities();
                return false;
            }
        }

        for (const auto& entityData : serializedEntities)
        {
            const Entity entity{ entityData.id };
            if (entityData.tag)
                world.AddComponent<Tag>(entity, *entityData.tag);
            if (entityData.transform)
                world.AddComponent<Transform>(entity, *entityData.transform);
            if (entityData.meshRenderer)
                world.AddComponent<MeshRenderer>(entity, *entityData.meshRenderer);
            if (entityData.hierarchy)
                world.AddComponent<Hierarchy>(entity);
            if (entityData.spinAnimation)
                world.AddComponent<SpinAnimation>(entity, *entityData.spinAnimation);
            if (entityData.camera)
                world.AddComponent<Camera>(entity, *entityData.camera);
            if (entityData.cameraController)
                world.AddComponent<CameraController>(entity, *entityData.cameraController);
        }

        for (const auto& entityData : serializedEntities)
        {
            if (!entityData.hierarchy || !entityData.hierarchy->parent)
                continue;

            const Entity child{ entityData.id };
            const Entity parent = entityData.hierarchy->parent;
            if (world.IsAlive(parent))
                AttachToParent(world, child, parent);
        }

        Logger::Info("Scene loaded from: ", path.string());
        return true;
    }
}
