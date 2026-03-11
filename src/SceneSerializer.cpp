#include "SceneSerializer.h"

#include "ECSComponents.h"
#include "Logger.h"
#include "MiniJson.h"

#include <fstream>
#include <optional>
#include <set>
#include <sstream>

namespace archi
{
    namespace
    {
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

        JsonValue MakeVec3Json(const Vec3& value)
        {
            JsonValue array = JsonValue::MakeArray();
            array.arrayValue.push_back(JsonValue::MakeNumber(value.x));
            array.arrayValue.push_back(JsonValue::MakeNumber(value.y));
            array.arrayValue.push_back(JsonValue::MakeNumber(value.z));
            return array;
        }

        JsonValue MakeVec4Json(const Vec4& value)
        {
            JsonValue array = JsonValue::MakeArray();
            array.arrayValue.push_back(JsonValue::MakeNumber(value.x));
            array.arrayValue.push_back(JsonValue::MakeNumber(value.y));
            array.arrayValue.push_back(JsonValue::MakeNumber(value.z));
            array.arrayValue.push_back(JsonValue::MakeNumber(value.w));
            return array;
        }

        bool ReadNumber(const JsonValue* value, double& outNumber)
        {
            if (!value || !value->IsNumber())
                return false;
            outNumber = value->numberValue;
            return true;
        }

        bool ReadBool(const JsonValue* value, bool& outBool)
        {
            if (!value || !value->IsBool())
                return false;
            outBool = value->boolValue;
            return true;
        }

        bool ReadString(const JsonValue* value, std::string& outString)
        {
            if (!value || !value->IsString())
                return false;
            outString = value->stringValue;
            return true;
        }

        bool ReadVec3(const JsonValue* value, Vec3& outVec)
        {
            if (!value || !value->IsArray() || value->arrayValue.size() != 3)
                return false;

            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            return ReadNumber(&value->arrayValue[0], x) &&
                ReadNumber(&value->arrayValue[1], y) &&
                ReadNumber(&value->arrayValue[2], z) &&
                ((outVec = Vec3{ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z) }), true);
        }

        bool ReadVec4(const JsonValue* value, Vec4& outVec)
        {
            if (!value || !value->IsArray() || value->arrayValue.size() != 4)
                return false;

            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            double w = 0.0;
            return ReadNumber(&value->arrayValue[0], x) &&
                ReadNumber(&value->arrayValue[1], y) &&
                ReadNumber(&value->arrayValue[2], z) &&
                ReadNumber(&value->arrayValue[3], w) &&
                ((outVec = Vec4{ static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), static_cast<float>(w) }), true);
        }

        const char* PrimitiveTypeToString(PrimitiveType primitive)
        {
            switch (primitive)
            {
            case PrimitiveType::Line:
                return "Line";
            case PrimitiveType::Triangle:
                return "Triangle";
            case PrimitiveType::Quad:
                return "Quad";
            case PrimitiveType::Cube:
                return "Cube";
            default:
                return "Triangle";
            }
        }

        bool TryParsePrimitiveType(const std::string& text, PrimitiveType& outPrimitive)
        {
            if (text == "Line")
            {
                outPrimitive = PrimitiveType::Line;
                return true;
            }
            if (text == "Triangle")
            {
                outPrimitive = PrimitiveType::Triangle;
                return true;
            }
            if (text == "Quad")
            {
                outPrimitive = PrimitiveType::Quad;
                return true;
            }
            if (text == "Cube")
            {
                outPrimitive = PrimitiveType::Cube;
                return true;
            }
            return false;
        }

        bool ParseEntity(const JsonValue& entityValue, SerializedEntityData& outEntity, std::string& outError)
        {
            if (!entityValue.IsObject())
            {
                outError = "Entity must be an object";
                return false;
            }

            double idNumber = 0.0;
            if (!ReadNumber(entityValue.Find("id"), idNumber))
            {
                outError = "Entity id is missing or invalid";
                return false;
            }

            outEntity.id = static_cast<EntityId>(idNumber);
            if (outEntity.id == InvalidEntityId)
            {
                outError = "Entity id must be non-zero";
                return false;
            }

            const JsonValue* components = entityValue.Find("components");
            if (!components || !components->IsObject())
            {
                outError = "Entity components object is missing";
                return false;
            }

            if (const JsonValue* tagValue = components->Find("Tag"))
            {
                if (!tagValue->IsObject())
                {
                    outError = "Tag component must be an object";
                    return false;
                }

                Tag tag{};
                if (!ReadString(tagValue->Find("name"), tag.name))
                {
                    outError = "Tag.name is missing or invalid";
                    return false;
                }
                outEntity.tag = tag;
            }

            if (const JsonValue* transformValue = components->Find("Transform"))
            {
                if (!transformValue->IsObject())
                {
                    outError = "Transform component must be an object";
                    return false;
                }

                Transform transform{};
                if (!ReadVec3(transformValue->Find("position"), transform.position) ||
                    !ReadVec3(transformValue->Find("rotation"), transform.rotation) ||
                    !ReadVec3(transformValue->Find("scale"), transform.scale))
                {
                    outError = "Transform component is invalid";
                    return false;
                }
                outEntity.transform = transform;
            }

            if (const JsonValue* meshValue = components->Find("MeshRenderer"))
            {
                if (!meshValue->IsObject())
                {
                    outError = "MeshRenderer component must be an object";
                    return false;
                }

                MeshRenderer mesh{};
                std::string primitiveText;
                if (!ReadString(meshValue->Find("primitive"), primitiveText) ||
                    !TryParsePrimitiveType(primitiveText, mesh.primitive) ||
                    !ReadVec4(meshValue->Find("color"), mesh.color))
                {
                    outError = "MeshRenderer component is invalid";
                    return false;
                }

                const JsonValue* texturePath = meshValue->Find("texturePath");
                if (texturePath && !texturePath->IsNull() && !ReadString(texturePath, mesh.texturePath))
                {
                    outError = "MeshRenderer.texturePath is invalid";
                    return false;
                }

                const JsonValue* materialName = meshValue->Find("materialName");
                if (materialName && !materialName->IsNull() && !ReadString(materialName, mesh.materialName))
                {
                    outError = "MeshRenderer.materialName is invalid";
                    return false;
                }

                outEntity.meshRenderer = mesh;
            }

            if (const JsonValue* hierarchyValue = components->Find("Hierarchy"))
            {
                if (!hierarchyValue->IsObject())
                {
                    outError = "Hierarchy component must be an object";
                    return false;
                }

                Hierarchy hierarchy{};
                if (const JsonValue* parentValue = hierarchyValue->Find("parent"))
                {
                    double parentId = 0.0;
                    if (!ReadNumber(parentValue, parentId))
                    {
                        outError = "Hierarchy.parent is invalid";
                        return false;
                    }
                    hierarchy.parent = Entity{ static_cast<EntityId>(parentId) };
                }

                if (const JsonValue* childrenValue = hierarchyValue->Find("children"))
                {
                    if (!childrenValue->IsArray())
                    {
                        outError = "Hierarchy.children must be an array";
                        return false;
                    }

                    for (const JsonValue& childValue : childrenValue->arrayValue)
                    {
                        double childId = 0.0;
                        if (!ReadNumber(&childValue, childId))
                        {
                            outError = "Hierarchy.children contains invalid id";
                            return false;
                        }
                        hierarchy.children.push_back(Entity{ static_cast<EntityId>(childId) });
                    }
                }

                outEntity.hierarchy = hierarchy;
            }

            if (const JsonValue* animationValue = components->Find("SpinAnimation"))
            {
                if (!animationValue->IsObject())
                {
                    outError = "SpinAnimation component must be an object";
                    return false;
                }

                SpinAnimation animation{};
                double amplitude = 0.0;
                double speed = 0.0;
                double elapsed = 0.0;
                if (!ReadVec3(animationValue->Find("angularVelocity"), animation.angularVelocity) ||
                    !ReadVec3(animationValue->Find("translationAxis"), animation.translationAxis) ||
                    !ReadNumber(animationValue->Find("translationAmplitude"), amplitude) ||
                    !ReadNumber(animationValue->Find("translationSpeed"), speed) ||
                    !ReadVec3(animationValue->Find("anchorPosition"), animation.anchorPosition) ||
                    !ReadNumber(animationValue->Find("elapsed"), elapsed))
                {
                    outError = "SpinAnimation component is invalid";
                    return false;
                }

                animation.translationAmplitude = static_cast<float>(amplitude);
                animation.translationSpeed = static_cast<float>(speed);
                animation.elapsed = elapsed;
                outEntity.spinAnimation = animation;
            }

            if (const JsonValue* cameraValue = components->Find("Camera"))
            {
                if (!cameraValue->IsObject())
                {
                    outError = "Camera component must be an object";
                    return false;
                }

                Camera camera{};
                double halfHeight = 0.0;
                double nearPlane = 0.0;
                double farPlane = 0.0;
                if (!ReadBool(cameraValue->Find("isPrimary"), camera.isPrimary) ||
                    !ReadNumber(cameraValue->Find("orthographicHalfHeight"), halfHeight) ||
                    !ReadNumber(cameraValue->Find("nearPlane"), nearPlane) ||
                    !ReadNumber(cameraValue->Find("farPlane"), farPlane))
                {
                    outError = "Camera component is invalid";
                    return false;
                }

                camera.orthographicHalfHeight = static_cast<float>(halfHeight);
                camera.nearPlane = static_cast<float>(nearPlane);
                camera.farPlane = static_cast<float>(farPlane);
                outEntity.camera = camera;
            }

            if (const JsonValue* controllerValue = components->Find("CameraController"))
            {
                if (!controllerValue->IsObject())
                {
                    outError = "CameraController component must be an object";
                    return false;
                }

                CameraController controller{};
                double moveSpeed = 0.0;
                double zoomSpeed = 0.0;
                if (!ReadNumber(controllerValue->Find("moveSpeed"), moveSpeed) ||
                    !ReadNumber(controllerValue->Find("zoomSpeed"), zoomSpeed))
                {
                    outError = "CameraController component is invalid";
                    return false;
                }

                controller.moveSpeed = static_cast<float>(moveSpeed);
                controller.zoomSpeed = static_cast<float>(zoomSpeed);
                outEntity.cameraController = controller;
            }

            return true;
        }

        std::string ReadTextFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::in | std::ios::binary);
            std::ostringstream ss;
            ss << file.rdbuf();
            return ss.str();
        }
    }

    bool SceneSerializer::SaveWorld(const World& world, const std::filesystem::path& path)
    {
        JsonValue root = JsonValue::MakeObject();
        root.objectValue["version"] = JsonValue::MakeNumber(1.0);

        JsonValue entities = JsonValue::MakeArray();
        world.ForEachEntity([&](Entity entity) {
            JsonValue entityValue = JsonValue::MakeObject();
            entityValue.objectValue["id"] = JsonValue::MakeNumber(static_cast<double>(entity.id));

            JsonValue components = JsonValue::MakeObject();

            if (const auto* tag = world.GetComponent<Tag>(entity))
            {
                JsonValue tagValue = JsonValue::MakeObject();
                tagValue.objectValue["name"] = JsonValue::MakeString(tag->name);
                components.objectValue["Tag"] = std::move(tagValue);
            }

            if (const auto* transform = world.GetComponent<Transform>(entity))
            {
                JsonValue transformValue = JsonValue::MakeObject();
                transformValue.objectValue["position"] = MakeVec3Json(transform->position);
                transformValue.objectValue["rotation"] = MakeVec3Json(transform->rotation);
                transformValue.objectValue["scale"] = MakeVec3Json(transform->scale);
                components.objectValue["Transform"] = std::move(transformValue);
            }

            if (const auto* mesh = world.GetComponent<MeshRenderer>(entity))
            {
                JsonValue meshValue = JsonValue::MakeObject();
                meshValue.objectValue["primitive"] = JsonValue::MakeString(PrimitiveTypeToString(mesh->primitive));
                meshValue.objectValue["color"] = MakeVec4Json(mesh->color);
                meshValue.objectValue["texturePath"] = mesh->texturePath.empty()
                    ? JsonValue::MakeNull()
                    : JsonValue::MakeString(mesh->texturePath);
                meshValue.objectValue["materialName"] = JsonValue::MakeString(mesh->materialName);
                components.objectValue["MeshRenderer"] = std::move(meshValue);
            }

            if (const auto* hierarchy = world.GetComponent<Hierarchy>(entity))
            {
                JsonValue hierarchyValue = JsonValue::MakeObject();
                hierarchyValue.objectValue["parent"] = JsonValue::MakeNumber(static_cast<double>(hierarchy->parent.id));

                JsonValue children = JsonValue::MakeArray();
                for (const Entity child : hierarchy->children)
                    children.arrayValue.push_back(JsonValue::MakeNumber(static_cast<double>(child.id)));
                hierarchyValue.objectValue["children"] = std::move(children);

                components.objectValue["Hierarchy"] = std::move(hierarchyValue);
            }

            if (const auto* animation = world.GetComponent<SpinAnimation>(entity))
            {
                JsonValue animationValue = JsonValue::MakeObject();
                animationValue.objectValue["angularVelocity"] = MakeVec3Json(animation->angularVelocity);
                animationValue.objectValue["translationAxis"] = MakeVec3Json(animation->translationAxis);
                animationValue.objectValue["translationAmplitude"] = JsonValue::MakeNumber(animation->translationAmplitude);
                animationValue.objectValue["translationSpeed"] = JsonValue::MakeNumber(animation->translationSpeed);
                animationValue.objectValue["anchorPosition"] = MakeVec3Json(animation->anchorPosition);
                animationValue.objectValue["elapsed"] = JsonValue::MakeNumber(animation->elapsed);
                components.objectValue["SpinAnimation"] = std::move(animationValue);
            }

            if (const auto* camera = world.GetComponent<Camera>(entity))
            {
                JsonValue cameraValue = JsonValue::MakeObject();
                cameraValue.objectValue["isPrimary"] = JsonValue::MakeBool(camera->isPrimary);
                cameraValue.objectValue["orthographicHalfHeight"] = JsonValue::MakeNumber(camera->orthographicHalfHeight);
                cameraValue.objectValue["nearPlane"] = JsonValue::MakeNumber(camera->nearPlane);
                cameraValue.objectValue["farPlane"] = JsonValue::MakeNumber(camera->farPlane);
                components.objectValue["Camera"] = std::move(cameraValue);
            }

            if (const auto* controller = world.GetComponent<CameraController>(entity))
            {
                JsonValue controllerValue = JsonValue::MakeObject();
                controllerValue.objectValue["moveSpeed"] = JsonValue::MakeNumber(controller->moveSpeed);
                controllerValue.objectValue["zoomSpeed"] = JsonValue::MakeNumber(controller->zoomSpeed);
                components.objectValue["CameraController"] = std::move(controllerValue);
            }

            entityValue.objectValue["components"] = std::move(components);
            entities.arrayValue.push_back(std::move(entityValue));
        });

        root.objectValue["entities"] = std::move(entities);

        if (const auto parentPath = path.parent_path(); !parentPath.empty())
            std::filesystem::create_directories(parentPath);

        std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file)
        {
            Logger::Error("Failed to open scene file for writing: ", path.string());
            return false;
        }

        file << WriteJson(root, true, 2);
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

        const std::string jsonText = ReadTextFile(path);
        JsonValue root{};
        std::string parseError;
        if (!ParseJson(jsonText, root, &parseError))
        {
            Logger::Error("Failed to parse scene JSON: ", parseError);
            return false;
        }

        const JsonValue* entitiesValue = root.Find("entities");
        if (!entitiesValue || !entitiesValue->IsArray())
        {
            Logger::Error("Scene JSON does not contain an entities array");
            return false;
        }

        std::vector<SerializedEntityData> serializedEntities{};
        std::set<EntityId> uniqueIds{};
        serializedEntities.reserve(entitiesValue->arrayValue.size());

        for (const JsonValue& entityValue : entitiesValue->arrayValue)
        {
            SerializedEntityData entityData{};
            std::string error;
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
