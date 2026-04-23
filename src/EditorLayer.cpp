#include "EditorLayer.h"

#include "Application.h"
#include "AssetPaths.h"
#include "ECSComponents.h"
#include "Logger.h"
#include "ResourceManager.h"
#include "SceneSerializer.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace archi
{
    namespace
    {
        using json = nlohmann::json;

        constexpr float kInspectorDragSpeed = 0.05f;
        constexpr float kMinViewportSize = 32.0f;

        std::string DescribeEntity(const World& world, Entity entity)
        {
            if (const auto* tag = world.GetComponent<Tag>(entity); tag && !tag->name.empty())
                return tag->name;
            return "Entity " + std::to_string(entity.id);
        }

        std::string HumanizePath(const std::string& assetPath)
        {
            return assetPath.empty() ? std::string("<None>") : assetPath;
        }

        bool DrawAssetCombo(const char* label, std::string& currentValue, const std::vector<std::string>& options)
        {
            bool changed = false;
            if (ImGui::BeginCombo(label, HumanizePath(currentValue).c_str()))
            {
                const bool noneSelected = currentValue.empty();
                if (ImGui::Selectable("<None>", noneSelected))
                {
                    currentValue.clear();
                    changed = true;
                }

                for (const std::string& option : options)
                {
                    const bool selected = (currentValue == option);
                    if (ImGui::Selectable(option.c_str(), selected))
                    {
                        currentValue = option;
                        changed = true;
                    }

                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }
            return changed;
        }

        std::vector<std::string> CollectAssets(
            const std::filesystem::path& relativeDirectory,
            const std::vector<std::string>& extensions)
        {
            std::vector<std::string> assets{};
            const std::filesystem::path assetsRoot = FindAssetsRoot();
            const std::filesystem::path directory = assetsRoot / relativeDirectory;
            if (!std::filesystem::exists(directory))
                return assets;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
            {
                if (!entry.is_regular_file())
                    continue;

                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });

                if (!extensions.empty() &&
                    std::find(extensions.begin(), extensions.end(), extension) == extensions.end())
                {
                    continue;
                }

                assets.push_back(std::filesystem::relative(entry.path(), assetsRoot).generic_string());
            }

            std::sort(assets.begin(), assets.end());
            assets.erase(std::unique(assets.begin(), assets.end()), assets.end());
            return assets;
        }

        std::string FormatBytes(std::size_t bytes)
        {
            static constexpr std::array<const char*, 4> kUnits{ "B", "KB", "MB", "GB" };
            double value = static_cast<double>(bytes);
            std::size_t unitIndex = 0;
            while (value >= 1024.0 && unitIndex + 1 < kUnits.size())
            {
                value /= 1024.0;
                ++unitIndex;
            }

            std::ostringstream stream{};
            stream.setf(std::ios::fixed);
            stream.precision(unitIndex == 0 ? 0 : 2);
            stream << value << ' ' << kUnits[unitIndex];
            return stream.str();
        }

        std::filesystem::path EditorHistoryDirectory()
        {
            const std::filesystem::path historyDir = GetWritableAssetPath("scenes/.editor_history");
            std::filesystem::create_directories(historyDir);
            return historyDir;
        }

        std::string MakeUniqueEntityName(const World& world, const std::string& baseName)
        {
            std::string candidate = baseName.empty() ? "Entity" : baseName;
            int suffix = 1;
            auto exists = [&](const std::string& value) {
                bool found = false;
                world.ForEach<Tag>([&](Entity, const Tag& tag) {
                    if (!found && tag.name == value)
                        found = true;
                });
                return found;
            };

            while (exists(candidate))
                candidate = (baseName.empty() ? "Entity" : baseName) + "_" + std::to_string(suffix++);
            return candidate;
        }

        bool SaveMaterialJson(const std::filesystem::path& path, const MaterialAsset& material)
        {
            json data = {
                { "shaderAsset", material.shaderAsset },
                { "textureAsset", material.textureAsset },
                { "baseColor", { material.baseColor.x, material.baseColor.y, material.baseColor.z, material.baseColor.w } },
                { "useTexture", material.useTexture },
                { "hotReload", material.hotReloadEnabled }
            };

            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file)
                return false;
            file << data.dump(2);
            return static_cast<bool>(file);
        }

        void SelectActiveCamera(const World& world, const Camera*& outCamera, const Transform*& outTransform)
        {
            outCamera = nullptr;
            outTransform = nullptr;

            const Camera* firstCamera = nullptr;
            const Transform* firstTransform = nullptr;

            world.ForEach<Camera, Transform>([&](Entity, const Camera& camera, const Transform& transform) {
                if (!firstCamera)
                {
                    firstCamera = &camera;
                    firstTransform = &transform;
                }

                if (!outCamera && camera.isPrimary)
                {
                    outCamera = &camera;
                    outTransform = &transform;
                }
            });

            if (!outCamera)
            {
                outCamera = firstCamera;
                outTransform = firstTransform;
            }
        }

        Mat4 BuildCameraView(const Camera* camera, const Transform* transform)
        {
            if (!camera || !transform)
                return Mat4::Identity();

            if (camera->usePerspective)
                return MakeLookAtMatrix(transform->position, transform->position + camera->forward, camera->up);

            return MakeViewMatrix(transform->position, transform->rotation);
        }

        Mat4 BuildCameraProjection(const Camera* camera, float aspectRatio)
        {
            if (!camera)
                return MakeOrthographicProjection(aspectRatio, 1.25f, -20.0f, 20.0f);

            if (camera->usePerspective)
                return MakePerspectiveProjection(camera->fovRadians, aspectRatio, camera->nearPlane, camera->farPlane);

            return MakeOrthographicProjection(aspectRatio, camera->orthographicHalfHeight, camera->nearPlane, camera->farPlane);
        }

        Mat4 ResolveWorldMatrix(
            const World& world,
            Entity entity,
            std::map<EntityId, Mat4>& cache,
            std::set<EntityId>& recursionStack)
        {
            const auto cached = cache.find(entity.id);
            if (cached != cache.end())
                return cached->second;

            const auto* transform = world.GetComponent<Transform>(entity);
            if (!transform)
                return Mat4::Identity();

            if (recursionStack.find(entity.id) != recursionStack.end())
                return MakeTransformMatrix(transform->position, transform->rotation, transform->scale);

            recursionStack.insert(entity.id);

            Mat4 worldMatrix = MakeTransformMatrix(transform->position, transform->rotation, transform->scale);
            if (const auto* hierarchy = world.GetComponent<Hierarchy>(entity))
            {
                if (hierarchy->parent && world.HasComponent<Transform>(hierarchy->parent))
                    worldMatrix = ResolveWorldMatrix(world, hierarchy->parent, cache, recursionStack) * worldMatrix;
            }

            recursionStack.erase(entity.id);
            cache[entity.id] = worldMatrix;
            return worldMatrix;
        }

        float SanitizeScaleComponent(float value)
        {
            if (std::abs(value) >= 0.001f)
                return value;
            return value < 0.0f ? -0.001f : 0.001f;
        }

        bool HasParent(const World& world, Entity entity)
        {
            if (const auto* hierarchy = world.GetComponent<Hierarchy>(entity))
                return hierarchy->parent && world.IsAlive(hierarchy->parent);
            return false;
        }

        bool IsSphereLikeMeshAsset(const std::string& meshAsset)
        {
            std::string normalized = meshAsset;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return normalized == "builtin:mesh:sphere" || normalized.find("sphere") != std::string::npos;
        }

        bool TryGetMeshLocalBounds(
            ResourceManager& resources,
            const std::string& meshAsset,
            Vec3& outCenter,
            Vec3& outHalfExtents)
        {
            if (meshAsset.empty())
                return false;

            const ResourcePtr<MeshAsset> meshResource = resources.Load<MeshAsset>(meshAsset);
            if (!meshResource || !meshResource->IsReady())
                return false;

            const Vec3 boundsMin = meshResource->value.mesh.boundsMin;
            const Vec3 boundsMax = meshResource->value.mesh.boundsMax;
            outCenter = {
                (boundsMin.x + boundsMax.x) * 0.5f,
                (boundsMin.y + boundsMax.y) * 0.5f,
                (boundsMin.z + boundsMax.z) * 0.5f
            };
            outHalfExtents = {
                std::max(0.05f, (boundsMax.x - boundsMin.x) * 0.5f),
                std::max(0.05f, (boundsMax.y - boundsMin.y) * 0.5f),
                std::max(0.05f, (boundsMax.z - boundsMin.z) * 0.5f)
            };
            return true;
        }

        Collider BuildDefaultColliderFromMesh(
            ResourceManager& resources,
            const std::string& meshAsset,
            int requestedType,
            float friction,
            float bounciness)
        {
            Vec3 center{ 0.0f, 0.0f, 0.0f };
            Vec3 halfExtents{ 0.5f, 0.5f, 0.5f };
            (void)TryGetMeshLocalBounds(resources, meshAsset, center, halfExtents);

            Collider collider{};
            const bool useSphere =
                (requestedType == 2) ||
                (requestedType == 0 && IsSphereLikeMeshAsset(meshAsset));
            collider.type = useSphere ? ColliderType::Sphere : ColliderType::Aabb;
            collider.offset = center;
            collider.halfExtents = halfExtents;
            collider.radius = std::max({ halfExtents.x, halfExtents.y, halfExtents.z });
            collider.friction = friction;
            collider.bounciness = bounciness;
            collider.debugColor = useSphere
                ? Vec4{ 0.25f, 0.82f, 1.0f, 0.9f }
                : Vec4{ 1.0f, 0.25f, 0.25f, 0.75f };
            return collider;
        }

        struct EditorRay
        {
            Vec3 origin{ 0.0f, 0.0f, 0.0f };
            Vec3 direction{ 0.0f, 0.0f, -1.0f };
        };

        struct EditorAabb
        {
            Vec3 min{ -0.5f, -0.5f, -0.5f };
            Vec3 max{ 0.5f, 0.5f, 0.5f };
        };

        struct EditorSphere
        {
            Vec3 center{ 0.0f, 0.0f, 0.0f };
            float radius = 0.5f;
        };

        EditorAabb ComputeEditorAabb(const Transform& transform, const Collider& collider)
        {
            const Vec3 absScale{
                std::abs(transform.scale.x),
                std::abs(transform.scale.y),
                std::abs(transform.scale.z)
            };

            Vec3 halfExtents = collider.halfExtents;
            if (collider.type == ColliderType::Sphere)
            {
                const float radius = collider.radius * std::max({ absScale.x, absScale.y, absScale.z });
                halfExtents = { radius, radius, radius };
            }
            else
            {
                halfExtents = {
                    std::max(0.01f, collider.halfExtents.x * absScale.x),
                    std::max(0.01f, collider.halfExtents.y * absScale.y),
                    std::max(0.01f, collider.halfExtents.z * absScale.z)
                };
            }

            const Vec3 center = transform.position + collider.offset;
            return {
                center - halfExtents,
                center + halfExtents
            };
        }

        EditorSphere ComputeEditorSphere(const Transform& transform, const Collider& collider)
        {
            const Vec3 absScale{
                std::abs(transform.scale.x),
                std::abs(transform.scale.y),
                std::abs(transform.scale.z)
            };
            return {
                transform.position + collider.offset,
                std::max(0.05f, collider.radius * std::max({ absScale.x, absScale.y, absScale.z }))
            };
        }

        bool IntersectRayAabb(const EditorRay& ray, const EditorAabb& aabb, float& outDistance)
        {
            float tMin = 0.0f;
            float tMax = 100000.0f;

            const auto intersectAxis = [&](float origin, float direction, float minValue, float maxValue) {
                if (std::abs(direction) <= 0.00001f)
                    return origin >= minValue && origin <= maxValue;

                float t1 = (minValue - origin) / direction;
                float t2 = (maxValue - origin) / direction;
                if (t1 > t2)
                    std::swap(t1, t2);

                tMin = std::max(tMin, t1);
                tMax = std::min(tMax, t2);
                return tMin <= tMax;
            };

            if (!intersectAxis(ray.origin.x, ray.direction.x, aabb.min.x, aabb.max.x) ||
                !intersectAxis(ray.origin.y, ray.direction.y, aabb.min.y, aabb.max.y) ||
                !intersectAxis(ray.origin.z, ray.direction.z, aabb.min.z, aabb.max.z))
            {
                return false;
            }

            outDistance = tMin >= 0.0f ? tMin : tMax;
            return outDistance >= 0.0f;
        }

        bool IntersectRaySphere(const EditorRay& ray, const EditorSphere& sphere, float& outDistance)
        {
            const Vec3 toCenter = ray.origin - sphere.center;
            const float a = Dot(ray.direction, ray.direction);
            const float b = 2.0f * Dot(toCenter, ray.direction);
            const float c = Dot(toCenter, toCenter) - sphere.radius * sphere.radius;
            const float discriminant = b * b - 4.0f * a * c;
            if (discriminant < 0.0f)
                return false;

            const float sqrtDisc = std::sqrt(discriminant);
            const float t0 = (-b - sqrtDisc) / (2.0f * a);
            const float t1 = (-b + sqrtDisc) / (2.0f * a);
            if (t0 >= 0.0f)
            {
                outDistance = t0;
                return true;
            }
            if (t1 >= 0.0f)
            {
                outDistance = t1;
                return true;
            }
            return false;
        }

        EditorRay BuildViewportRay(
            const Vec2& viewportScreenPosition,
            const Vec2& viewportSize,
            const Vec2& mousePosition,
            const Mat4& view,
            const Mat4& projection)
        {
            const float ndcX =
                ((mousePosition.x - viewportScreenPosition.x) / std::max(viewportSize.x, 1.0f)) * 2.0f - 1.0f;
            const float ndcY =
                1.0f - ((mousePosition.y - viewportScreenPosition.y) / std::max(viewportSize.y, 1.0f)) * 2.0f;

            const Mat4 inverseViewProjection = Inverse(projection * view);
            const Vec3 nearPoint = TransformPoint(inverseViewProjection, { ndcX, ndcY, -1.0f });
            const Vec3 farPoint = TransformPoint(inverseViewProjection, { ndcX, ndcY, 1.0f });

            return { nearPoint, Normalize(farPoint - nearPoint) };
        }

        Entity PickViewportEntity(
            const World& world,
            ResourceManager& resources,
            const EditorRay& ray)
        {
            Entity picked{};
            float closestDistance = 100000.0f;

            world.ForEach<Transform>([&](Entity entity, const Transform& transform) {
                float hitDistance = 0.0f;
                bool hit = false;

                if (const auto* collider = world.GetComponent<Collider>(entity))
                {
                    hit =
                        collider->type == ColliderType::Sphere
                        ? IntersectRaySphere(ray, ComputeEditorSphere(transform, *collider), hitDistance)
                        : IntersectRayAabb(ray, ComputeEditorAabb(transform, *collider), hitDistance);
                }
                else if (const auto* meshRenderer = world.GetComponent<MeshRenderer>(entity))
                {
                    Vec3 localCenter{ 0.0f, 0.0f, 0.0f };
                    Vec3 localHalfExtents{ 0.5f, 0.5f, 0.5f };
                    if (TryGetMeshLocalBounds(resources, meshRenderer->meshAsset, localCenter, localHalfExtents))
                    {
                        const Vec3 scaledHalfExtents{
                            std::max(0.05f, localHalfExtents.x * std::abs(transform.scale.x)),
                            std::max(0.05f, localHalfExtents.y * std::abs(transform.scale.y)),
                            std::max(0.05f, localHalfExtents.z * std::abs(transform.scale.z))
                        };
                        const Vec3 center = transform.position + localCenter;
                        const EditorAabb meshBounds{ center - scaledHalfExtents, center + scaledHalfExtents };
                        hit = IntersectRayAabb(ray, meshBounds, hitDistance);
                    }
                }

                if (hit && hitDistance < closestDistance)
                {
                    closestDistance = hitDistance;
                    picked = entity;
                }
            });

            return picked;
        }
    }

    bool EditorLayer::Init(IRenderAdapter& renderer)
    {
        if (m_initialized)
            return true;

        GLFWwindow* window = static_cast<GLFWwindow*>(renderer.PrimaryWindowHandle());
        if (!window)
        {
            Logger::Error("Editor init failed: renderer does not expose a GLFW window");
            return false;
        }
        m_primaryWindowHandle = window;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        ApplyStyle();

        if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
        {
            Logger::Error("ImGui GLFW backend init failed");
            return false;
        }

        if (!ImGui_ImplOpenGL3_Init("#version 330"))
        {
            Logger::Error("ImGui OpenGL3 backend init failed");
            ImGui_ImplGlfw_Shutdown();
            return false;
        }

        m_gizmoOperation = static_cast<int>(ImGuizmo::TRANSLATE);
        m_gizmoMode = static_cast<int>(ImGuizmo::WORLD);
        m_initialized = true;
        Logger::Info("Editor layer initialized");
        return true;
    }

    void EditorLayer::Shutdown()
    {
        if (!m_initialized)
            return;

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_primaryWindowHandle = nullptr;
        m_initialized = false;
    }

    void EditorLayer::BeginFrame()
    {
        if (!m_initialized)
            return;

        if (m_primaryWindowHandle)
            glfwMakeContextCurrent(static_cast<GLFWwindow*>(m_primaryWindowHandle));

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
    }

    float EditorLayer::SceneAspectRatio() const
    {
        return m_viewportHeight > 0
            ? static_cast<float>(m_viewportWidth) / static_cast<float>(m_viewportHeight)
            : 16.0f / 9.0f;
    }

    void EditorLayer::Build(Application& app, double dt)
    {
        if (!m_initialized)
            return;

        m_lastFrameMs = static_cast<float>(dt * 1000.0);
        m_fpsAccumulator += dt;
        ++m_fpsFrameCounter;
        if (m_fpsAccumulator >= 1.0)
        {
            m_averageFps = static_cast<float>(m_fpsFrameCounter / m_fpsAccumulator);
            m_fpsAccumulator = 0.0;
            m_fpsFrameCounter = 0;
        }

        m_playMode = app.IsEditorPlayMode();

        ImGuiIO& io = ImGui::GetIO();
        if (m_playMode)
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        else
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        const World& world = app.SceneWorld();
        if (m_selectedEntity && !world.IsAlive(m_selectedEntity))
            m_selectedEntity = {};
        if (!m_selectedEntity)
            m_selectedEntity = app.ControlledEntity();

        m_allowGameplayInput = false;
        m_allowCameraInput = false;
        m_skipEditingInteractionsThisFrame = false;
        m_viewportHovered = false;
        m_viewportFocused = false;
        m_viewportWidth = 0;
        m_viewportHeight = 0;
        m_viewportScreenPosition = {};
        m_requestMaterialPreview = false;

        const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->Pos);
        ImGui::SetNextWindowSize(mainViewport->Size);
        ImGui::SetNextWindowViewport(mainViewport->ID);
        ImGuiWindowFlags hostWindowFlags =
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("EditorDockspaceHost", nullptr, hostWindowFlags);
        ImGui::PopStyleVar(3);

        const ImGuiID dockspaceId = ImGui::GetID("EditorDockspace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        DrawMainMenuBar(app);
        ImGui::End();

        DrawToolbar(app);
        if (m_showHierarchy)
            DrawHierarchyPanel(app);
        if (m_showInspector)
            DrawInspectorPanel(app);
        if (m_showMaterialEditor)
            DrawMaterialEditorPanel(app);
        if (m_showAssetBrowser)
            DrawAssetBrowserPanel(app);
        if (m_showStatistics)
            DrawStatisticsPanel(app, dt);
        if (m_showViewport)
            DrawViewportPanel(app);
        RenderMaterialPreview(app, dt);
        DrawAboutPopup();

        if (m_showDemoWindow)
            ImGui::ShowDemoWindow(&m_showDemoWindow);

        if (!io.WantCaptureKeyboard)
        {
            if (app.Input().WasActionJustPressed("TogglePhysicsDebug"))
            {
                app.TogglePhysicsDebug();
                Logger::Info("Physics debug rendering: ", app.IsPhysicsDebugEnabled() ? "ON" : "OFF");
            }

            if (app.Input().WasActionJustPressed("OpenWindow"))
                app.OpenAdditionalWindow();

            if (app.Input().WasActionJustPressed("ReloadAssets"))
                app.RequestShaderReload();

            if (!m_playMode)
            {
                if (app.Input().WasActionJustPressed("SaveScene"))
                    app.SaveScene();
                if (app.Input().WasActionJustPressed("LoadScene"))
                    app.LoadScene();
                if (app.Input().WasActionJustPressed("ResetScene"))
                    app.ResetSceneToDefault();
            }
        }

        if (!m_playMode &&
            !io.WantTextInput &&
            m_selectedEntity &&
            app.SceneWorld().IsAlive(m_selectedEntity) &&
            ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            CaptureUndoSnapshot(app);
            const Entity entityToDelete = m_selectedEntity;
            m_selectedEntity = {};
            app.DeleteSceneEntity(entityToDelete);
        }

        const bool gizmoBusy = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        const bool viewportActive = m_viewportHovered || m_viewportFocused;
        const bool textEditingActive = io.WantTextInput;

        // Dear ImGui may keep WantCaptureKeyboard=true because of keyboard navigation.
        // For editor camera movement we only need to block input while the user is typing
        // into a text widget or actively manipulating gizmos.
        m_allowCameraInput = viewportActive && !textEditingActive && !gizmoBusy;
        m_allowGameplayInput = m_playMode && viewportActive && !textEditingActive && !gizmoBusy;
    }

    void EditorLayer::Render()
    {
        if (!m_initialized)
            return;

        if (m_primaryWindowHandle)
            glfwMakeContextCurrent(static_cast<GLFWwindow*>(m_primaryWindowHandle));

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void EditorLayer::ApplyStyle()
    {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.22f, 0.28f, 0.38f, 0.80f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.38f, 0.52f, 0.90f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.24f, 0.32f, 0.90f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.38f, 0.50f, 1.00f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.15f, 0.18f, 1.00f);
    }

    void EditorLayer::DrawMainMenuBar(Application& app)
    {
        if (!ImGui::BeginMenuBar())
            return;

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save Scene", "K", false, !m_playMode))
                app.SaveScene();
            if (ImGui::MenuItem("Load Scene", "L", false, !m_playMode))
                app.LoadScene();
            if (ImGui::MenuItem("Reset Scene", "M", false, !m_playMode))
                app.ResetSceneToDefault();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
                app.RequestQuit();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", nullptr, false, !m_playMode && !m_undoSnapshots.empty()))
                Undo(app);
            if (ImGui::MenuItem("Redo", nullptr, false, !m_playMode && !m_redoSnapshots.empty()))
                Redo(app);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Scene Hierarchy", nullptr, &m_showHierarchy);
            ImGui::MenuItem("Inspector", nullptr, &m_showInspector);
            ImGui::MenuItem("Material Editor", nullptr, &m_showMaterialEditor);
            ImGui::MenuItem("Asset Browser", nullptr, &m_showAssetBrowser);
            ImGui::MenuItem("Statistics", nullptr, &m_showStatistics);
            ImGui::MenuItem("Viewport", nullptr, &m_showViewport);
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &m_showDemoWindow);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("About"))
                m_showAboutPopup = true;
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    void EditorLayer::DrawToolbar(Application& app)
    {
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;

        if (!ImGui::Begin("Toolbar", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        if (!m_playMode)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.40f, 0.18f, 1.0f));
            if (ImGui::Button("Play") && app.EnterEditorPlayMode())
                m_playMode = true;
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
            if (ImGui::Button("Stop") && app.ExitEditorPlayMode())
                m_playMode = false;
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        if (ImGui::Button("Translate"))
            m_gizmoOperation = static_cast<int>(ImGuizmo::TRANSLATE);
        ImGui::SameLine();
        if (ImGui::Button("Rotate"))
            m_gizmoOperation = static_cast<int>(ImGuizmo::ROTATE);
        ImGui::SameLine();
        if (ImGui::Button("Scale"))
            m_gizmoOperation = static_cast<int>(ImGuizmo::SCALE);

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::RadioButton("World", m_gizmoMode == static_cast<int>(ImGuizmo::WORLD)))
            m_gizmoMode = static_cast<int>(ImGuizmo::WORLD);
        ImGui::SameLine();
        if (ImGui::RadioButton("Local", m_gizmoMode == static_cast<int>(ImGuizmo::LOCAL)))
            m_gizmoMode = static_cast<int>(ImGuizmo::LOCAL);

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::Button("Save") && !m_playMode)
            app.SaveScene();
        ImGui::SameLine();
        if (ImGui::Button("Load") && !m_playMode)
            app.LoadScene();
        ImGui::SameLine();
        if (ImGui::Button("Reset") && !m_playMode)
            app.ResetSceneToDefault();
        ImGui::SameLine();
        if (ImGui::Button("Undo") && !m_playMode)
            Undo(app);
        ImGui::SameLine();
        if (ImGui::Button("Redo") && !m_playMode)
            Redo(app);
        ImGui::SameLine();
        if (ImGui::Button("Shaders"))
            app.RequestShaderReload();
        ImGui::SameLine();
        if (ImGui::Button("Extra Window"))
            app.OpenAdditionalWindow();
        ImGui::SameLine();

        bool debugPhysics = app.IsPhysicsDebugEnabled();
        if (ImGui::Checkbox("Physics Debug", &debugPhysics))
            app.TogglePhysicsDebug();

        ImGui::SameLine();
        ImGui::TextDisabled("%s mode", m_playMode ? "Play" : "Edit");
        ImGui::End();
    }

    bool EditorLayer::DrawHierarchyNode(Application& app, Entity entity)
    {
        World& world = app.SceneWorld();
        const auto* hierarchy = world.GetComponent<Hierarchy>(entity);
        const bool hasChildren = hierarchy && !hierarchy->children.empty();
        const std::string label = DescribeEntity(world, entity);

        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_SpanAvailWidth |
            ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (m_selectedEntity == entity)
            flags |= ImGuiTreeNodeFlags_Selected;

        const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<std::uintptr_t>(entity.id)), flags, "%s", label.c_str());
        if (ImGui::IsItemClicked())
            m_selectedEntity = entity;

        if (open)
        {
            if (hasChildren)
            {
                for (Entity child : hierarchy->children)
                {
                    if (world.IsAlive(child))
                        DrawHierarchyNode(app, child);
                }
            }
            ImGui::TreePop();
        }

        return open;
    }

    void EditorLayer::DrawHierarchyPanel(Application& app)
    {
        if (!ImGui::Begin("Scene Hierarchy", &m_showHierarchy))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("Entities: %zu", app.SceneWorld().EntityCount());
        ImGui::Separator();

        std::vector<Entity> roots{};
        app.SceneWorld().ForEachEntity([&](Entity entity) {
            if (!HasParent(app.SceneWorld(), entity))
                roots.push_back(entity);
        });
        std::sort(roots.begin(), roots.end(), [](Entity lhs, Entity rhs) { return lhs.id < rhs.id; });

        for (Entity entity : roots)
            DrawHierarchyNode(app, entity);

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
            m_selectedEntity = {};

        ImGui::End();
    }

    void EditorLayer::DrawSelectedEntityInspector(Application& app, Entity entity)
    {
        World& world = app.SceneWorld();
        const bool editEnabled = !m_playMode;

        auto captureEdit = [&]() {
            if (editEnabled)
                CaptureUndoSnapshot(app);
        };

        auto editString = [&](const char* label, std::string& value) {
            std::string temp = value;
            const bool changed = ImGui::InputText(label, &temp);
            const bool activated = ImGui::IsItemActivated();
            if (activated)
                captureEdit();
            if (changed && temp != value)
            {
                if (!activated && !ImGui::IsItemActive())
                    captureEdit();
                value = std::move(temp);
                return true;
            }
            return false;
        };

        auto editVec3 = [&](const char* label, Vec3& value, float speed, float minValue = 0.0f, float maxValue = 0.0f, bool useRange = false) {
            float temp[3] = { value.x, value.y, value.z };
            const bool changed = useRange
                ? ImGui::DragFloat3(label, temp, speed, minValue, maxValue)
                : ImGui::DragFloat3(label, temp, speed);
            const bool activated = ImGui::IsItemActivated();
            if (activated)
                captureEdit();
            if (changed)
            {
                if (!activated && !ImGui::IsItemActive())
                    captureEdit();
                value = { temp[0], temp[1], temp[2] };
                return true;
            }
            return false;
        };

        auto editVec4Color = [&](const char* label, Vec4& value) {
            float temp[4] = { value.x, value.y, value.z, value.w };
            const bool changed = ImGui::ColorEdit4(label, temp);
            const bool activated = ImGui::IsItemActivated();
            if (activated)
                captureEdit();
            if (changed)
            {
                if (!activated && !ImGui::IsItemActive())
                    captureEdit();
                value = { temp[0], temp[1], temp[2], temp[3] };
                return true;
            }
            return false;
        };

        auto editFloat = [&](const char* label, float& value, float speed, float minValue = 0.0f, float maxValue = 0.0f, const char* format = "%.3f", bool useRange = false) {
            float temp = value;
            const bool changed = useRange
                ? ImGui::DragFloat(label, &temp, speed, minValue, maxValue, format)
                : ImGui::DragFloat(label, &temp, speed, 0.0f, 0.0f, format);
            const bool activated = ImGui::IsItemActivated();
            if (activated)
                captureEdit();
            if (changed)
            {
                if (!activated && !ImGui::IsItemActive())
                    captureEdit();
                value = temp;
                return true;
            }
            return false;
        };

        auto editBool = [&](const char* label, bool& value) {
            bool temp = value;
            const bool changed = ImGui::Checkbox(label, &temp);
            if (changed && temp != value)
            {
                captureEdit();
                value = temp;
                return true;
            }
            return false;
        };

        auto editAssetCombo = [&](const char* label, std::string& value, const std::vector<std::string>& options) {
            std::string temp = value;
            if (DrawAssetCombo(label, temp, options) && temp != value)
            {
                captureEdit();
                value = std::move(temp);
                return true;
            }
            return false;
        };

        if (auto* tag = world.GetComponent<Tag>(entity))
        {
            if (ImGui::TreeNodeEx("Tag", ImGuiTreeNodeFlags_DefaultOpen))
            {
                editString("Name", tag->name);
                ImGui::TreePop();
            }
        }

        if (auto* transform = world.GetComponent<Transform>(entity))
        {
            if (ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                editVec3("Position", transform->position, kInspectorDragSpeed);

                float rotationDegrees[3] = {
                    Degrees(transform->rotation.x),
                    Degrees(transform->rotation.y),
                    Degrees(transform->rotation.z)
                };
                const bool rotationChanged = ImGui::DragFloat3("Rotation", rotationDegrees, 0.5f);
                const bool rotationActivated = ImGui::IsItemActivated();
                if (rotationActivated)
                    captureEdit();
                if (rotationChanged)
                {
                    if (!rotationActivated && !ImGui::IsItemActive())
                        captureEdit();
                    transform->rotation.x = Radians(rotationDegrees[0]);
                    transform->rotation.y = Radians(rotationDegrees[1]);
                    transform->rotation.z = Radians(rotationDegrees[2]);
                }

                if (editVec3("Scale", transform->scale, 0.02f))
                {
                    transform->scale.x = SanitizeScaleComponent(transform->scale.x);
                    transform->scale.y = SanitizeScaleComponent(transform->scale.y);
                    transform->scale.z = SanitizeScaleComponent(transform->scale.z);
                }

                ImGui::TreePop();
            }
        }

        if (auto* meshRenderer = world.GetComponent<MeshRenderer>(entity))
        {
            if (ImGui::TreeNodeEx("MeshRenderer", ImGuiTreeNodeFlags_DefaultOpen))
            {
                std::vector<std::string> meshAssets = CollectAssets("models", { ".obj", ".fbx", ".gltf", ".glb", ".dae" });
                meshAssets.insert(meshAssets.begin(), "builtin:mesh:sphere");
                meshAssets.erase(std::unique(meshAssets.begin(), meshAssets.end()), meshAssets.end());
                std::sort(meshAssets.begin(), meshAssets.end());
                editAssetCombo("Mesh", meshRenderer->meshAsset, meshAssets);

                const std::vector<std::string> materialAssets = CollectAssets("materials", { ".json" });
                if (editAssetCombo("Material", meshRenderer->materialAsset, materialAssets))
                    m_selectedMaterialAsset = meshRenderer->materialAsset;
                ImGui::SameLine();
                if (ImGui::Button("Edit Material") && !meshRenderer->materialAsset.empty())
                {
                    m_selectedMaterialAsset = meshRenderer->materialAsset;
                    m_showMaterialEditor = true;
                }

                const std::vector<std::string> textureAssets = CollectAssets("textures", { ".png", ".jpg", ".jpeg", ".bmp", ".tga" });
                editAssetCombo("Texture Override", meshRenderer->textureAsset, textureAssets);

                const std::vector<std::string> shaderAssets = CollectAssets("shaders", { ".json" });
                editAssetCombo("Shader Override", meshRenderer->shaderAsset, shaderAssets);

                editVec4Color("Tint", meshRenderer->tintColor);
                editBool("Async Load", meshRenderer->asyncLoad);
                ImGui::TreePop();
            }
        }

        if (ImGui::TreeNodeEx("Physics Setup", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const bool hasRigidbody = world.HasComponent<Rigidbody>(entity);
            const bool hasCollider = world.HasComponent<Collider>(entity);
            const auto* meshRenderer = world.GetComponent<MeshRenderer>(entity);
            bool physicsEnabled = hasRigidbody || hasCollider;
            const bool physicsWasEnabled = physicsEnabled;

            if (ImGui::Checkbox("Enable Physics", &physicsEnabled) && physicsEnabled != physicsWasEnabled)
            {
                captureEdit();
                if (physicsEnabled)
                {
                    if (!hasCollider)
                    {
                        Collider collider = BuildDefaultColliderFromMesh(
                            app.Resources(),
                            meshRenderer ? meshRenderer->meshAsset : std::string{},
                            m_spawnColliderType,
                            m_spawnFriction,
                            m_spawnBounciness);
                        world.AddComponent<Collider>(entity, collider);
                    }

                    if (!hasRigidbody)
                    {
                        Rigidbody rigidbody{};
                        rigidbody.mass = m_spawnMass;
                        rigidbody.useGravity = m_spawnUseGravity;
                        rigidbody.isStatic = m_spawnIsStatic;
                        world.AddComponent<Rigidbody>(entity, rigidbody);
                    }
                }
                else
                {
                    if (hasRigidbody)
                        world.RemoveComponent<Rigidbody>(entity);
                    if (hasCollider)
                        world.RemoveComponent<Collider>(entity);
                }
            }

            ImGui::TextDisabled(physicsEnabled ? "Physics components are attached" : "Physics components are not attached");
            if (!physicsEnabled)
                ImGui::TextDisabled("Default values come from Asset Browser spawn settings.");

            ImGui::TreePop();
        }

        if (auto* body = world.GetComponent<Rigidbody>(entity))
        {
            if (ImGui::TreeNodeEx("Rigidbody", ImGuiTreeNodeFlags_DefaultOpen))
            {
                editFloat("Mass", body->mass, 0.05f, 0.0f, 1000.0f, "%.3f", true);
                editVec3("Acceleration", body->acceleration, 0.05f);
                editBool("Use Gravity", body->useGravity);
                editBool("Static", body->isStatic);
                ImGui::Text("Velocity: %.2f %.2f %.2f", body->velocity.x, body->velocity.y, body->velocity.z);
                ImGui::Text("Grounded: %s", body->isGrounded ? "true" : "false");
                ImGui::TreePop();
            }
        }

        if (auto* collider = world.GetComponent<Collider>(entity))
        {
            if (ImGui::TreeNodeEx("Collider", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const char* colliderTypes[] = { "AABB", "Sphere" };
                int colliderType = (collider->type == ColliderType::Sphere) ? 1 : 0;
                if (ImGui::Combo("Type", &colliderType, colliderTypes, IM_ARRAYSIZE(colliderTypes)))
                {
                    captureEdit();
                    collider->type = colliderType == 0 ? ColliderType::Aabb : ColliderType::Sphere;
                }

                if (collider->type == ColliderType::Aabb)
                    editVec3("Half Extents", collider->halfExtents, 0.02f, 0.01f, 100.0f, true);
                else
                    editFloat("Radius", collider->radius, 0.02f, 0.01f, 100.0f, "%.3f", true);

                editVec3("Offset", collider->offset, 0.02f);
                editBool("Trigger", collider->isTrigger);
                editFloat("Friction", collider->friction, 0.01f, 0.0f, 1.0f, "%.3f", true);
                editFloat("Bounciness", collider->bounciness, 0.01f, 0.0f, 1.0f, "%.3f", true);
                editVec4Color("Debug Color", collider->debugColor);
                ImGui::TreePop();
            }
        }

        if (auto* camera = world.GetComponent<Camera>(entity))
        {
            if (ImGui::TreeNodeEx("Camera", ImGuiTreeNodeFlags_DefaultOpen))
            {
                editBool("Primary", camera->isPrimary);
                editBool("Perspective", camera->usePerspective);
                if (camera->usePerspective)
                {
                    float fovDegrees = Degrees(camera->fovRadians);
                    const bool fovChanged = ImGui::DragFloat("FOV", &fovDegrees, 0.25f, 10.0f, 170.0f);
                    const bool fovActivated = ImGui::IsItemActivated();
                    if (fovActivated)
                        captureEdit();
                    if (fovChanged)
                    {
                        if (!fovActivated && !ImGui::IsItemActive())
                            captureEdit();
                        camera->fovRadians = Radians(fovDegrees);
                    }
                }
                else
                {
                    editFloat("Ortho Half Height", camera->orthographicHalfHeight, 0.05f, 0.1f, 100.0f, "%.3f", true);
                }

                editFloat("Near Plane", camera->nearPlane, 0.01f, 0.001f, 1000.0f, "%.3f", true);
                editFloat("Far Plane", camera->farPlane, 0.1f, camera->nearPlane + 0.1f, 5000.0f, "%.3f", true);
                ImGui::TreePop();
            }
        }

        if (auto* controller = world.GetComponent<CameraController>(entity))
        {
            if (ImGui::TreeNodeEx("CameraController", ImGuiTreeNodeFlags_DefaultOpen))
            {
                editFloat("Move Speed", controller->moveSpeed, 0.05f, 0.1f, 100.0f, "%.3f", true);
                editFloat("Vertical Speed", controller->verticalSpeed, 0.05f, 0.1f, 100.0f, "%.3f", true);
                editFloat("Boost", controller->boostMultiplier, 0.05f, 1.0f, 10.0f, "%.3f", true);
                editFloat("Mouse Sensitivity", controller->mouseSensitivity, 0.0001f, 0.0001f, 0.02f, "%.4f", true);
                editBool("Require RMB", controller->requireMouseButtonToLook);
                ImGui::TreePop();
            }
        }

        if (const auto* hierarchy = world.GetComponent<Hierarchy>(entity))
        {
            if (ImGui::TreeNodeEx("Hierarchy", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (hierarchy->parent)
                    ImGui::Text("Parent: %s", DescribeEntity(world, hierarchy->parent).c_str());
                else
                    ImGui::TextDisabled("Parent: <none>");

                ImGui::Text("Children: %zu", hierarchy->children.size());
                for (Entity child : hierarchy->children)
                    ImGui::BulletText("%s", DescribeEntity(world, child).c_str());

                ImGui::TreePop();
            }
        }
    }

    void EditorLayer::DrawInspectorPanel(Application& app)
    {
        if (!ImGui::Begin("Inspector", &m_showInspector))
        {
            ImGui::End();
            return;
        }

        if (!m_selectedEntity || !app.SceneWorld().IsAlive(m_selectedEntity))
        {
            ImGui::TextDisabled("No entity selected");
            ImGui::End();
            return;
        }

        ImGui::Text("Entity ID: %u", m_selectedEntity.id);
        ImGui::Separator();

        const bool disableEditing = m_playMode || m_skipEditingInteractionsThisFrame;
        if (disableEditing)
            ImGui::BeginDisabled();
        DrawSelectedEntityInspector(app, m_selectedEntity);
        if (disableEditing)
            ImGui::EndDisabled();

        if (disableEditing)
        {
            ImGui::Separator();
            ImGui::TextDisabled("Inspector editing is disabled in Play mode");
        }
        else
        {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.24f, 0.24f, 1.0f));
            if (ImGui::Button("Delete Entity"))
            {
                CaptureUndoSnapshot(app);
                const Entity entityToDelete = m_selectedEntity;
                m_selectedEntity = {};
                app.DeleteSceneEntity(entityToDelete);
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::End();
    }

    void EditorLayer::DrawMaterialEditorPanel(Application& app)
    {
        if (!ImGui::Begin("Material Editor", &m_showMaterialEditor))
        {
            ImGui::End();
            return;
        }

        const std::vector<std::string> materialAssets = CollectAssets("materials", { ".json" });
        const std::vector<std::string> shaderAssets = CollectAssets("shaders", { ".json" });
        const std::vector<std::string> textureAssets = CollectAssets("textures", { ".png", ".jpg", ".jpeg", ".bmp", ".tga" });

        if (m_selectedMaterialAsset.empty())
        {
            if (m_selectedEntity)
            {
                if (const auto* meshRenderer = app.SceneWorld().GetComponent<MeshRenderer>(m_selectedEntity);
                    meshRenderer && !meshRenderer->materialAsset.empty())
                {
                    m_selectedMaterialAsset = meshRenderer->materialAsset;
                }
            }

            if (m_selectedMaterialAsset.empty() && !materialAssets.empty())
                m_selectedMaterialAsset = materialAssets.front();
        }

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float sidebarWidth = std::min(260.0f, available.x * 0.35f);

        ImGui::BeginChild("MaterialList", ImVec2(sidebarWidth, 0.0f), true);
        if (ImGui::Button("Use Selected Entity Material"))
        {
            if (m_selectedEntity)
            {
                if (const auto* meshRenderer = app.SceneWorld().GetComponent<MeshRenderer>(m_selectedEntity);
                    meshRenderer && !meshRenderer->materialAsset.empty())
                {
                    m_selectedMaterialAsset = meshRenderer->materialAsset;
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("New Material"))
        {
            MaterialAsset material{};
            if (!m_selectedMaterialAsset.empty())
            {
                if (const auto selected = app.Resources().Load<MaterialAsset>(m_selectedMaterialAsset);
                    selected && selected->IsReady())
                {
                    material = selected->value;
                }
            }
            else if (const auto defaultMaterial = app.Resources().DefaultMaterial();
                defaultMaterial && defaultMaterial->IsReady())
            {
                material = defaultMaterial->value;
            }

            int suffix = 1;
            std::string relativePath{};
            std::filesystem::path fullPath{};
            do
            {
                relativePath = "materials/generated_material_" + std::to_string(suffix++) + ".json";
                fullPath = ResolveAssetPath(relativePath);
            } while (std::filesystem::exists(fullPath));

            material.sourcePath = fullPath;
            if (SaveMaterialJson(fullPath, material))
            {
                app.Resources().Reload<MaterialAsset>(relativePath);
                m_selectedMaterialAsset = relativePath;
            }
        }

        ImGui::Separator();
        for (const std::string& materialAsset : materialAssets)
        {
            const bool selected = (m_selectedMaterialAsset == materialAsset);
            if (ImGui::Selectable(materialAsset.c_str(), selected))
                m_selectedMaterialAsset = materialAsset;

            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("ASSET_MATERIAL", materialAsset.c_str(), materialAsset.size() + 1u);
                ImGui::TextUnformatted(materialAsset.c_str());
                ImGui::EndDragDropSource();
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("MaterialDetails", ImVec2(0.0f, 0.0f), true);
        if (m_selectedMaterialAsset.empty())
        {
            ImGui::TextDisabled("No material selected");
            ImGui::EndChild();
            ImGui::End();
            return;
        }

        const ResourcePtr<MaterialAsset> materialResource = app.Resources().Load<MaterialAsset>(m_selectedMaterialAsset);
        if (!materialResource || !materialResource->IsReady())
        {
            ImGui::TextDisabled("Material is still loading...");
            ImGui::EndChild();
            ImGui::End();
            return;
        }

        MaterialAsset& material = materialResource->value;
        bool changed = false;

        ImGui::TextWrapped("Editing: %s", m_selectedMaterialAsset.c_str());
        ImGui::Separator();

        std::string shaderAsset = material.shaderAsset;
        if (DrawAssetCombo("Shader", shaderAsset, shaderAssets) && shaderAsset != material.shaderAsset)
        {
            material.shaderAsset = std::move(shaderAsset);
            changed = true;
        }

        std::string textureAsset = material.textureAsset;
        if (DrawAssetCombo("Texture", textureAsset, textureAssets) && textureAsset != material.textureAsset)
        {
            material.textureAsset = std::move(textureAsset);
            changed = true;
        }

        float baseColor[4] = { material.baseColor.x, material.baseColor.y, material.baseColor.z, material.baseColor.w };
        if (ImGui::ColorEdit4("Base Color", baseColor))
        {
            material.baseColor = { baseColor[0], baseColor[1], baseColor[2], baseColor[3] };
            changed = true;
        }

        changed |= ImGui::Checkbox("Use Texture", &material.useTexture);
        changed |= ImGui::Checkbox("Hot Reload", &material.hotReloadEnabled);

        if (changed)
            SaveMaterialAsset(app, m_selectedMaterialAsset);

        ImGui::Separator();
        if (ImGui::Button("Save Material"))
            SaveMaterialAsset(app, m_selectedMaterialAsset);
        ImGui::SameLine();
        if (ImGui::Button("Reload Material"))
            app.Resources().Reload<MaterialAsset>(m_selectedMaterialAsset);

        ImGui::Separator();
        ImGui::TextUnformatted("Preview");
        ImGui::RadioButton("Sphere", &m_materialPreviewMeshMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Cube", &m_materialPreviewMeshMode, 1);

        const ImVec2 previewSize = ImVec2(
            std::max(140.0f, ImGui::GetContentRegionAvail().x),
            std::max(220.0f, std::min(320.0f, ImGui::GetContentRegionAvail().y)));
        m_materialPreviewWidth = static_cast<int>(previewSize.x);
        m_materialPreviewHeight = static_cast<int>(previewSize.y);
        m_requestMaterialPreview = true;

        const ImTextureID previewTexture = static_cast<ImTextureID>(app.Renderer().MaterialPreviewTextureHandle());
        if (previewTexture != 0)
        {
            ImGui::Image(previewTexture, previewSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        }
        else
        {
            ImGui::Dummy(previewSize);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - previewSize.y + 12.0f);
            ImGui::TextDisabled("Preview is warming up...");
        }

        ImGui::EndChild();
        ImGui::End();
    }

    void EditorLayer::DrawAssetBrowserPanel(Application& app)
    {
        if (!ImGui::Begin("Asset Browser", &m_showAssetBrowser))
        {
            ImGui::End();
            return;
        }

        const ResourceManager::CacheStats stats = app.Resources().GetCacheStats();
        ImGui::Text("Loaded: %zu mesh | %zu tex | %zu shader | %zu material",
            stats.meshCount,
            stats.textureCount,
            stats.shaderCount,
            stats.materialCount);
        ImGui::TextDisabled("Drag assets from this panel into the Viewport to spawn scene objects.");
        if (ImGui::CollapsingHeader("Spawn Physics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Enable Physics", &m_spawnWithPhysics);
            if (m_spawnWithPhysics)
            {
                const char* colliderModes[] = { "Auto", "AABB", "Sphere" };
                ImGui::Combo("Collider Type", &m_spawnColliderType, colliderModes, IM_ARRAYSIZE(colliderModes));
                ImGui::Checkbox("Use Gravity", &m_spawnUseGravity);
                ImGui::Checkbox("Static", &m_spawnIsStatic);
                ImGui::DragFloat("Mass", &m_spawnMass, 0.05f, 0.0f, 1000.0f, "%.3f");
                ImGui::DragFloat("Friction", &m_spawnFriction, 0.01f, 0.0f, 1.0f, "%.3f");
                ImGui::DragFloat("Bounciness", &m_spawnBounciness, 0.01f, 0.0f, 1.0f, "%.3f");
            }
        }
        ImGui::Separator();

        const auto drawAssetSection =
            [&](const char* label,
                const std::vector<std::string>& assets,
                const char* payloadType,
                bool selectInMaterialEditor) {
                if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
                    return;

                if (assets.empty())
                {
                    ImGui::TextDisabled("No assets found");
                    return;
                }

                for (const std::string& asset : assets)
                {
                    const bool selected = selectInMaterialEditor && (asset == m_selectedMaterialAsset);
                    if (ImGui::Selectable(asset.c_str(), selected) && selectInMaterialEditor)
                    {
                        m_selectedMaterialAsset = asset;
                        m_showMaterialEditor = true;
                    }

                    if (ImGui::IsItemHovered() && selectInMaterialEditor && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        m_selectedMaterialAsset = asset;
                        m_showMaterialEditor = true;
                    }

                    if (ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload(payloadType, asset.c_str(), asset.size() + 1u);
                        ImGui::TextUnformatted(asset.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
            };

        std::vector<std::string> meshAssets = CollectAssets("models", { ".obj", ".fbx", ".gltf", ".glb", ".dae" });
        meshAssets.insert(meshAssets.begin(), "builtin:mesh:sphere");
        meshAssets.erase(std::unique(meshAssets.begin(), meshAssets.end()), meshAssets.end());
        std::sort(meshAssets.begin(), meshAssets.end());

        drawAssetSection("Meshes", meshAssets, "ASSET_MESH", false);
        drawAssetSection("Materials", CollectAssets("materials", { ".json" }), "ASSET_MATERIAL", true);
        drawAssetSection("Textures", CollectAssets("textures", { ".png", ".jpg", ".jpeg", ".bmp", ".tga" }), "ASSET_TEXTURE", false);
        drawAssetSection("Shaders", CollectAssets("shaders", { ".json" }), "ASSET_SHADER", false);

        ImGui::End();
    }

    void EditorLayer::RenderMaterialPreview(Application& app, double)
    {
        if (!m_requestMaterialPreview ||
            m_selectedMaterialAsset.empty() ||
            m_materialPreviewWidth <= 0 ||
            m_materialPreviewHeight <= 0)
        {
            return;
        }

        const ResourcePtr<MaterialAsset> materialResource = app.Resources().Load<MaterialAsset>(m_selectedMaterialAsset);
        if (!materialResource || !materialResource->IsReady())
            return;

        const std::string meshAsset = (m_materialPreviewMeshMode == 0) ? "builtin:mesh:sphere" : "models/cube.obj";
        const ResourcePtr<MeshAsset> meshResource = app.Resources().Load<MeshAsset>(meshAsset);
        if (!meshResource || !meshResource->IsReady() || meshResource->value.gpuHandle == InvalidMeshHandle)
            return;

        std::string shaderAsset = materialResource->value.shaderAsset.empty()
            ? std::string("shaders/textured.shader.json")
            : materialResource->value.shaderAsset;
        const ResourcePtr<ShaderAsset> shaderResource = app.Resources().Load<ShaderAsset>(shaderAsset);
        if (!shaderResource || !shaderResource->IsReady() || shaderResource->value.gpuHandle == InvalidShaderHandle)
            return;

        TextureHandle textureHandle = InvalidTextureHandle;
        bool useTexture = false;
        if (materialResource->value.useTexture && !materialResource->value.textureAsset.empty())
        {
            const ResourcePtr<TextureAsset> textureResource = app.Resources().Load<TextureAsset>(materialResource->value.textureAsset);
            if (textureResource && textureResource->IsReady() && textureResource->value.gpuHandle != InvalidTextureHandle)
            {
                textureHandle = textureResource->value.gpuHandle;
                useTexture = true;
            }
        }

        if (!app.Renderer().BeginMaterialPreviewRender(m_materialPreviewWidth, m_materialPreviewHeight))
            return;

        const float aspectRatio =
            static_cast<float>(m_materialPreviewWidth) / static_cast<float>(std::max(1, m_materialPreviewHeight));
        const float timeSeconds = static_cast<float>(glfwGetTime());
        const Mat4 model =
            MakeRotationYMatrix(timeSeconds * 0.65f) *
            MakeRotationXMatrix(Radians(-20.0f));
        const Mat4 view = MakeLookAtMatrix({ 0.0f, 0.4f, 3.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
        const Mat4 projection = MakePerspectiveProjection(Radians(45.0f), aspectRatio, 0.1f, 20.0f);

        RenderMeshCommand command{};
        command.mesh = meshResource->value.gpuHandle;
        command.texture = textureHandle;
        command.shader = shaderResource->value.gpuHandle;
        command.model = model;
        command.view = view;
        command.projection = projection;
        command.color = materialResource->value.baseColor;
        command.useTexture = useTexture;
        app.Renderer().DrawMesh(command);
        app.Renderer().EndMaterialPreviewRender();
    }

    void EditorLayer::DrawStatisticsPanel(Application& app, double)
    {
        if (!ImGui::Begin("Statistics", &m_showStatistics))
        {
            ImGui::End();
            return;
        }

        std::size_t renderableEntities = 0;
        app.SceneWorld().ForEach<MeshRenderer>([&](Entity, const MeshRenderer&) { ++renderableEntities; });

        const ResourceManager::CacheStats resourceStats = app.Resources().GetCacheStats();

        ImGui::Text("Mode: %s", m_playMode ? "Play" : "Edit");
        ImGui::Text("FPS: %.1f", m_averageFps);
        ImGui::Text("Frame Time: %.3f ms", m_lastFrameMs);
        ImGui::Separator();
        ImGui::Text("Entities Total: %zu", app.SceneWorld().EntityCount());
        ImGui::Text("Renderable Entities: %zu", renderableEntities);
        ImGui::Text("Drawn Objects: %zu", app.LastRenderedObjectCount());
        ImGui::Text("Render Pass Time: %.3f ms", app.LastRenderDurationMs());
        ImGui::Text("Active Collisions: %zu", app.ActiveCollisionCount());
        ImGui::Separator();
        ImGui::Text("Viewport: %d x %d", m_viewportWidth, m_viewportHeight);
        ImGui::Text("Physics Debug: %s", app.IsPhysicsDebugEnabled() ? "ON" : "OFF");
        ImGui::Separator();
        ImGui::Text("Meshes: %zu", resourceStats.meshCount);
        ImGui::Text("Textures: %zu", resourceStats.textureCount);
        ImGui::Text("Shaders: %zu", resourceStats.shaderCount);
        ImGui::Text("Materials: %zu", resourceStats.materialCount);
        ImGui::Text("Resource Memory: %s", FormatBytes(resourceStats.approximateBytes).c_str());

        ImGui::End();
    }

    void EditorLayer::DrawViewportPanel(Application& app)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const bool open = ImGui::Begin("Viewport", &m_showViewport);
        ImGui::PopStyleVar();
        if (!open)
        {
            ImGui::End();
            return;
        }

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 imagePosition = ImGui::GetCursorScreenPos();
        m_viewportScreenPosition = { imagePosition.x, imagePosition.y };
        m_viewportWidth = static_cast<int>(std::max(available.x, kMinViewportSize));
        m_viewportHeight = static_cast<int>(std::max(available.y, kMinViewportSize));

        const ImTextureID textureId =
            static_cast<ImTextureID>(app.Renderer().ViewportTextureHandle());

        if (textureId != 0 && available.x > 1.0f && available.y > 1.0f)
        {
            ImGui::Image(textureId, available, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        }
        else
        {
            ImGui::Dummy(available);
            ImGui::SetCursorScreenPos(ImVec2(imagePosition.x + 14.0f, imagePosition.y + 14.0f));
            ImGui::TextDisabled("Viewport preview is warming up...");
        }

        m_viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        m_viewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        AcceptViewportAssetDrops(app);

        if (!m_skipEditingInteractionsThisFrame &&
            textureId != 0 &&
            m_viewportHovered &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGuizmo::IsOver() &&
            !ImGuizmo::IsUsing())
        {
            const Camera* activeCamera = nullptr;
            const Transform* activeCameraTransform = nullptr;
            SelectActiveCamera(app.SceneWorld(), activeCamera, activeCameraTransform);

            const float aspectRatio = SceneAspectRatio();
            const Mat4 view = BuildCameraView(activeCamera, activeCameraTransform);
            const Mat4 projection = BuildCameraProjection(activeCamera, aspectRatio);
            const ImVec2 mousePosition = ImGui::GetMousePos();
            const EditorRay ray = BuildViewportRay(
                { imagePosition.x, imagePosition.y },
                { available.x, available.y },
                { mousePosition.x, mousePosition.y },
                view,
                projection);
            m_selectedEntity = PickViewportEntity(app.SceneWorld(), app.Resources(), ray);
        }

        if (!m_skipEditingInteractionsThisFrame &&
            textureId != 0 &&
            m_selectedEntity &&
            !m_playMode &&
            app.SceneWorld().HasComponent<Transform>(m_selectedEntity))
        {
            const Camera* activeCamera = nullptr;
            const Transform* activeCameraTransform = nullptr;
            SelectActiveCamera(app.SceneWorld(), activeCamera, activeCameraTransform);

            const float aspectRatio = SceneAspectRatio();
            const Mat4 view = BuildCameraView(activeCamera, activeCameraTransform);
            const Mat4 projection = BuildCameraProjection(activeCamera, aspectRatio);

            std::map<EntityId, Mat4> matrixCache{};
            std::set<EntityId> recursionStack{};
            Mat4 worldMatrix = ResolveWorldMatrix(app.SceneWorld(), m_selectedEntity, matrixCache, recursionStack);

            Mat4 parentWorld = Mat4::Identity();
            bool hasParentMatrix = false;
            if (const auto* hierarchy = app.SceneWorld().GetComponent<Hierarchy>(m_selectedEntity))
            {
                if (hierarchy->parent && app.SceneWorld().HasComponent<Transform>(hierarchy->parent))
                {
                    parentWorld = ResolveWorldMatrix(app.SceneWorld(), hierarchy->parent, matrixCache, recursionStack);
                    hasParentMatrix = true;
                }
            }

            ImGuizmo::SetOrthographic(!(activeCamera && activeCamera->usePerspective));
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(imagePosition.x, imagePosition.y, available.x, available.y);

            Mat4 gizmoMatrix = worldMatrix;
            const bool manipulated = ImGuizmo::Manipulate(
                view.data(),
                projection.data(),
                static_cast<ImGuizmo::OPERATION>(m_gizmoOperation),
                static_cast<ImGuizmo::MODE>(m_gizmoMode),
                gizmoMatrix.data());

            const bool gizmoUsing = ImGuizmo::IsUsing();
            if (gizmoUsing && !m_gizmoWasUsing)
                CaptureUndoSnapshot(app);

            if (manipulated)
            {
                Mat4 localMatrix = hasParentMatrix ? (Inverse(parentWorld) * gizmoMatrix) : gizmoMatrix;

                float translation[3]{};
                float rotation[3]{};
                float scale[3]{};
                ImGuizmo::DecomposeMatrixToComponents(localMatrix.data(), translation, rotation, scale);

                if (auto* transform = app.SceneWorld().GetComponent<Transform>(m_selectedEntity))
                {
                    transform->position = { translation[0], translation[1], translation[2] };
                    transform->rotation = {
                        Radians(rotation[0]),
                        Radians(rotation[1]),
                        Radians(rotation[2])
                    };
                    transform->scale = {
                        SanitizeScaleComponent(scale[0]),
                        SanitizeScaleComponent(scale[1]),
                        SanitizeScaleComponent(scale[2])
                    };
                }
            }
        }

        m_gizmoWasUsing = ImGuizmo::IsUsing();

        ImGui::End();
    }

    void EditorLayer::CaptureUndoSnapshot(Application& app)
    {
        if (m_playMode)
            return;

        const std::filesystem::path historyDir = EditorHistoryDirectory();
        std::ostringstream fileName{};
        fileName << "undo_" << std::setw(8) << std::setfill('0') << ++m_snapshotCounter << ".json";
        const std::filesystem::path snapshotPath = historyDir / fileName.str();
        if (!app.SaveSceneToPath(snapshotPath))
        {
            Logger::Warn("Failed to capture editor undo snapshot: ", snapshotPath.string());
            return;
        }

        m_undoSnapshots.push_back(snapshotPath);
        for (const auto& redoPath : m_redoSnapshots)
            std::filesystem::remove(redoPath);
        m_redoSnapshots.clear();

        constexpr std::size_t kMaxHistoryEntries = 64;
        while (m_undoSnapshots.size() > kMaxHistoryEntries)
        {
            std::filesystem::remove(m_undoSnapshots.front());
            m_undoSnapshots.erase(m_undoSnapshots.begin());
        }
    }

    bool EditorLayer::Undo(Application& app)
    {
        if (m_playMode || m_undoSnapshots.empty())
            return false;

        const std::filesystem::path historyDir = EditorHistoryDirectory();
        std::ostringstream redoName{};
        redoName << "redo_" << std::setw(8) << std::setfill('0') << ++m_snapshotCounter << ".json";
        const std::filesystem::path redoSnapshot = historyDir / redoName.str();
        if (!app.SaveSceneToPath(redoSnapshot))
            return false;

        m_redoSnapshots.push_back(redoSnapshot);

        const std::filesystem::path snapshotPath = m_undoSnapshots.back();
        m_undoSnapshots.pop_back();
        if (!app.LoadSceneFromPath(snapshotPath))
            return false;

        m_gizmoWasUsing = false;
        m_skipEditingInteractionsThisFrame = true;
        if (m_selectedEntity && !app.SceneWorld().IsAlive(m_selectedEntity))
            m_selectedEntity = app.ControlledEntity();

        Logger::Info("Editor undo applied");
        return true;
    }

    bool EditorLayer::Redo(Application& app)
    {
        if (m_playMode || m_redoSnapshots.empty())
            return false;

        const std::filesystem::path historyDir = EditorHistoryDirectory();
        std::ostringstream undoName{};
        undoName << "undo_" << std::setw(8) << std::setfill('0') << ++m_snapshotCounter << ".json";
        const std::filesystem::path undoSnapshot = historyDir / undoName.str();
        if (!app.SaveSceneToPath(undoSnapshot))
            return false;

        m_undoSnapshots.push_back(undoSnapshot);

        const std::filesystem::path snapshotPath = m_redoSnapshots.back();
        m_redoSnapshots.pop_back();
        if (!app.LoadSceneFromPath(snapshotPath))
            return false;

        m_gizmoWasUsing = false;
        m_skipEditingInteractionsThisFrame = true;
        if (m_selectedEntity && !app.SceneWorld().IsAlive(m_selectedEntity))
            m_selectedEntity = app.ControlledEntity();

        Logger::Info("Editor redo applied");
        return true;
    }

    bool EditorLayer::SaveMaterialAsset(Application& app, const std::string& materialAssetPath)
    {
        if (materialAssetPath.empty())
            return false;

        const ResourcePtr<MaterialAsset> materialResource = app.Resources().Load<MaterialAsset>(materialAssetPath);
        if (!materialResource || !materialResource->IsReady())
            return false;

        const std::filesystem::path fullPath = ResolveAssetPath(materialAssetPath);
        if (!SaveMaterialJson(fullPath, materialResource->value))
        {
            Logger::Warn("Failed to save material asset: ", materialAssetPath);
            return false;
        }

        const ResourcePtr<MaterialAsset> reloaded = app.Resources().Reload<MaterialAsset>(materialAssetPath);
        const bool ok = reloaded && reloaded->IsReady();
        if (ok)
            Logger::Info("Material saved from editor: ", materialAssetPath);
        return ok;
    }

    bool EditorLayer::AcceptViewportAssetDrops(Application& app)
    {
        bool handled = false;
        if (!ImGui::BeginDragDropTarget())
            return false;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MESH"))
        {
            const char* path = static_cast<const char*>(payload->Data);
            if (path && *path)
            {
                CreateEntityFromAsset(app, path, "materials/checker.material.json", "");
                handled = true;
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MATERIAL"))
        {
            const char* path = static_cast<const char*>(payload->Data);
            if (path && *path)
            {
                if (m_selectedEntity)
                {
                    if (auto* meshRenderer = app.SceneWorld().GetComponent<MeshRenderer>(m_selectedEntity))
                    {
                        CaptureUndoSnapshot(app);
                        meshRenderer->materialAsset = path;
                        meshRenderer->textureAsset.clear();
                        meshRenderer->shaderAsset.clear();
                        m_selectedMaterialAsset = path;
                    }
                    else
                    {
                        CreateEntityFromAsset(app, "models/cube.obj", path, "");
                    }
                }
                else
                {
                    CreateEntityFromAsset(app, "models/cube.obj", path, "");
                }
                handled = true;
            }
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE"))
        {
            const char* path = static_cast<const char*>(payload->Data);
            if (path && *path)
            {
                CreateEntityFromAsset(app, "models/cube.obj", "", path);
                handled = true;
            }
        }

        ImGui::EndDragDropTarget();
        return handled;
    }

    Entity EditorLayer::CreateEntityFromAsset(
        Application& app,
        const std::string& meshAsset,
        const std::string& materialAsset,
        const std::string& textureOverride)
    {
        CaptureUndoSnapshot(app);

        std::string baseName = meshAsset;
        if (meshAsset == "builtin:mesh:sphere")
            baseName = "Sphere";
        else if (const std::filesystem::path meshPath{ meshAsset }; !meshPath.stem().empty())
            baseName = meshPath.stem().string();

        World& world = app.SceneWorld();
        const Entity entity = world.CreateEntity();
        world.AddComponent<Tag>(entity).name = MakeUniqueEntityName(world, baseName);

        Vec3 spawnPosition{ 0.0f, 1.0f, 0.0f };
        const Camera* activeCamera = nullptr;
        const Transform* activeCameraTransform = nullptr;
        SelectActiveCamera(world, activeCamera, activeCameraTransform);
        if (activeCamera && activeCameraTransform)
        {
            spawnPosition =
                activeCameraTransform->position +
                Normalize(activeCamera->forward) * 4.0f;
            if (spawnPosition.y < 0.5f)
                spawnPosition.y = 0.5f;
        }

        auto& transform = world.AddComponent<Transform>(entity);
        transform.position = spawnPosition;
        transform.rotation = { 0.0f, 0.0f, 0.0f };
        transform.scale = { 1.0f, 1.0f, 1.0f };

        auto& meshRenderer = world.AddComponent<MeshRenderer>(entity);
        meshRenderer.meshAsset = meshAsset;
        meshRenderer.materialAsset = materialAsset;
        meshRenderer.textureAsset = textureOverride;
        meshRenderer.shaderAsset = textureOverride.empty() ? std::string{} : std::string("shaders/textured.shader.json");
        meshRenderer.tintColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        meshRenderer.asyncLoad = true;

        if (m_spawnWithPhysics)
        {
            Collider collider = BuildDefaultColliderFromMesh(
                app.Resources(),
                meshAsset,
                m_spawnColliderType,
                m_spawnFriction,
                m_spawnBounciness);
            world.AddComponent<Collider>(entity, collider);

            Rigidbody rigidbody{};
            rigidbody.mass = m_spawnMass;
            rigidbody.useGravity = m_spawnUseGravity;
            rigidbody.isStatic = m_spawnIsStatic;
            world.AddComponent<Rigidbody>(entity, rigidbody);
        }

        app.Resources().Load<MeshAsset>(meshAsset);
        if (!materialAsset.empty())
            app.Resources().Load<MaterialAsset>(materialAsset);
        if (!textureOverride.empty())
            app.Resources().Load<TextureAsset>(textureOverride);

        m_selectedEntity = entity;
        Logger::Info("Editor spawned entity from asset: ", meshAsset);
        return entity;
    }

    void EditorLayer::DrawAboutPopup()
    {
        if (m_showAboutPopup)
        {
            ImGui::OpenPopup("About Archi Editor");
            m_showAboutPopup = false;
        }

        if (ImGui::BeginPopupModal("About Archi Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("ArchiEngine Editor");
            ImGui::Separator();
            ImGui::TextWrapped("WYSIWYG scene editor built with Dear ImGui and ImGuizmo.");
            ImGui::TextWrapped("The editor supports hierarchy browsing, live component editing, statistics and transform gizmos.");
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
}
