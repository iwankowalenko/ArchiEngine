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
            std::optional<Rigidbody> rigidbody{};
            std::optional<Collider> collider{};
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

        std::string ColliderTypeToString(ColliderType type)
        {
            switch (type)
            {
            case ColliderType::Sphere: return "Sphere";
            case ColliderType::Aabb:
            default: return "AABB";
            }
        }

        ColliderType ColliderTypeFromString(const std::string& typeName)
        {
            return typeName == "Sphere" ? ColliderType::Sphere : ColliderType::Aabb;
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

                // Migrate the old low-poly demo sphere to the built-in smooth sphere mesh.
                if (mesh.meshAsset == "models/sphere.obj")
                    mesh.meshAsset = "builtin:mesh:sphere";

                outEntity.meshRenderer = mesh;
            }

            if (components.contains("Rigidbody"))
            {
                Rigidbody rigidbody{};
                const json& value = components["Rigidbody"];
                if (value.contains("velocity") && !ReadVec3(value["velocity"], rigidbody.velocity))
                {
                    outError = "Rigidbody.velocity is invalid";
                    return false;
                }
                if (value.contains("acceleration") && !ReadVec3(value["acceleration"], rigidbody.acceleration))
                {
                    outError = "Rigidbody.acceleration is invalid";
                    return false;
                }
                rigidbody.mass = value.value("mass", 1.0f);
                rigidbody.useGravity = value.value("useGravity", true);
                rigidbody.isStatic = value.value("isStatic", false);
                rigidbody.isGrounded = value.value("isGrounded", false);
                outEntity.rigidbody = rigidbody;
            }

            if (components.contains("Collider"))
            {
                Collider collider{};
                const json& value = components["Collider"];
                collider.type = ColliderTypeFromString(value.value("type", std::string("AABB")));
                if (value.contains("halfExtents") && !ReadVec3(value["halfExtents"], collider.halfExtents))
                {
                    outError = "Collider.halfExtents is invalid";
                    return false;
                }
                if (value.contains("offset") && !ReadVec3(value["offset"], collider.offset))
                {
                    outError = "Collider.offset is invalid";
                    return false;
                }
                collider.radius = value.value("radius", 0.5f);
                collider.isTrigger = value.value("isTrigger", false);
                collider.friction = value.value("friction", 0.45f);
                collider.bounciness = value.value("bounciness", 0.0f);
                if (value.contains("debugColor") && !ReadVec4(value["debugColor"], collider.debugColor))
                {
                    outError = "Collider.debugColor is invalid";
                    return false;
                }
                outEntity.collider = collider;
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
                camera.usePerspective = value.value("usePerspective", value.contains("fovRadians"));
                camera.orthographicHalfHeight = value.value("orthographicHalfHeight", 1.25f);
                camera.nearPlane = value.value("nearPlane", camera.usePerspective ? 0.1f : -20.0f);
                camera.farPlane = value.value("farPlane", camera.usePerspective ? 100.0f : 20.0f);
                camera.fovRadians = value.value("fovRadians", Radians(value.value("fovDegrees", 60.0f)));
                camera.yaw = value.value("yaw", -1.57079632679f);
                camera.pitch = value.value("pitch", -0.26179938779f);
                if (value.contains("forward") && !ReadVec3(value["forward"], camera.forward))
                {
                    outError = "Camera.forward is invalid";
                    return false;
                }
                if (value.contains("up") && !ReadVec3(value["up"], camera.up))
                {
                    outError = "Camera.up is invalid";
                    return false;
                }
                outEntity.camera = camera;
            }

            if (components.contains("CameraController"))
            {
                CameraController controller{};
                const json& value = components["CameraController"];
                controller.moveSpeed = value.value("moveSpeed", 6.0f);
                controller.zoomSpeed = value.value("zoomSpeed", 1.2f);
                controller.verticalSpeed = value.value("verticalSpeed", 4.0f);
                controller.boostMultiplier = value.value("boostMultiplier", 2.0f);
                controller.mouseSensitivity = value.value("mouseSensitivity", 0.0035f);
                controller.requireMouseButtonToLook = value.value("requireMouseButtonToLook", true);
                outEntity.cameraController = controller;
            }

            return true;
        }
    }

    bool SceneSerializer::SaveWorld(const World& world, const std::filesystem::path& path)
    {
        json root{};
        root["version"] = 6;
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

            if (const auto* rigidbody = world.GetComponent<Rigidbody>(entity))
            {
                components["Rigidbody"] = {
                    { "velocity", ToJson(rigidbody->velocity) },
                    { "acceleration", ToJson(rigidbody->acceleration) },
                    { "mass", rigidbody->mass },
                    { "useGravity", rigidbody->useGravity },
                    { "isStatic", rigidbody->isStatic },
                    { "isGrounded", rigidbody->isGrounded }
                };
            }

            if (const auto* collider = world.GetComponent<Collider>(entity))
            {
                components["Collider"] = {
                    { "type", ColliderTypeToString(collider->type) },
                    { "halfExtents", ToJson(collider->halfExtents) },
                    { "offset", ToJson(collider->offset) },
                    { "radius", collider->radius },
                    { "isTrigger", collider->isTrigger },
                    { "friction", collider->friction },
                    { "bounciness", collider->bounciness },
                    { "debugColor", ToJson(collider->debugColor) }
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
                    { "usePerspective", camera->usePerspective },
                    { "forward", ToJson(camera->forward) },
                    { "up", ToJson(camera->up) },
                    { "fovRadians", camera->fovRadians },
                    { "orthographicHalfHeight", camera->orthographicHalfHeight },
                    { "nearPlane", camera->nearPlane },
                    { "farPlane", camera->farPlane },
                    { "yaw", camera->yaw },
                    { "pitch", camera->pitch }
                };
            }

            if (const auto* controller = world.GetComponent<CameraController>(entity))
            {
                components["CameraController"] = {
                    { "moveSpeed", controller->moveSpeed },
                    { "zoomSpeed", controller->zoomSpeed },
                    { "verticalSpeed", controller->verticalSpeed },
                    { "boostMultiplier", controller->boostMultiplier },
                    { "mouseSensitivity", controller->mouseSensitivity },
                    { "requireMouseButtonToLook", controller->requireMouseButtonToLook }
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
            if (entityData.rigidbody)
                world.AddComponent<Rigidbody>(entity, *entityData.rigidbody);
            if (entityData.collider)
                world.AddComponent<Collider>(entity, *entityData.collider);
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
