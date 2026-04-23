#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ECS.h"
#include "Math2D.h"

namespace archi
{
    class Application;
    class IRenderAdapter;

    class EditorLayer final
    {
    public:
        bool Init(IRenderAdapter& renderer);
        void Shutdown();

        void BeginFrame();
        void Build(Application& app, double dt);
        void Render();

        Entity SelectedEntity() const { return m_selectedEntity; }
        bool IsPlayMode() const { return m_playMode; }
        bool AllowGameplayInput() const { return m_allowGameplayInput; }
        bool AllowCameraInput() const { return m_allowCameraInput; }
        bool HasViewport() const { return m_viewportWidth > 16 && m_viewportHeight > 16; }
        int ViewportWidth() const { return m_viewportWidth; }
        int ViewportHeight() const { return m_viewportHeight; }
        float SceneAspectRatio() const;

    private:
        void ApplyStyle();
        void DrawMainMenuBar(Application& app);
        void DrawToolbar(Application& app);
        void DrawHierarchyPanel(Application& app);
        void DrawInspectorPanel(Application& app);
        void DrawStatisticsPanel(Application& app, double dt);
        void DrawViewportPanel(Application& app);
        void DrawMaterialEditorPanel(Application& app);
        void DrawAssetBrowserPanel(Application& app);
        void DrawAboutPopup();
        void RenderMaterialPreview(Application& app, double dt);

        bool DrawHierarchyNode(Application& app, Entity entity);
        void DrawSelectedEntityInspector(Application& app, Entity entity);
        void CaptureUndoSnapshot(Application& app);
        bool Undo(Application& app);
        bool Redo(Application& app);
        bool SaveMaterialAsset(Application& app, const std::string& materialAssetPath);
        bool AcceptViewportAssetDrops(Application& app);
        Entity CreateEntityFromAsset(Application& app, const std::string& meshAsset, const std::string& materialAsset, const std::string& textureOverride);

    private:
        bool m_initialized = false;
        Entity m_selectedEntity{};
        bool m_showHierarchy = true;
        bool m_showInspector = true;
        bool m_showStatistics = true;
        bool m_showViewport = true;
        bool m_showMaterialEditor = true;
        bool m_showAssetBrowser = true;
        bool m_showDemoWindow = false;
        bool m_showAboutPopup = false;
        bool m_playMode = false;
        bool m_allowGameplayInput = false;
        bool m_allowCameraInput = false;
        bool m_skipEditingInteractionsThisFrame = false;
        bool m_gizmoWasUsing = false;
        bool m_viewportHovered = false;
        bool m_viewportFocused = false;
        int m_viewportWidth = 0;
        int m_viewportHeight = 0;
        Vec2 m_viewportScreenPosition{};
        int m_gizmoOperation = 0;
        int m_gizmoMode = 0;
        int m_materialPreviewMeshMode = 0;
        double m_fpsAccumulator = 0.0;
        int m_fpsFrameCounter = 0;
        float m_averageFps = 0.0f;
        float m_lastFrameMs = 0.0f;
        void* m_primaryWindowHandle = nullptr;
        std::string m_selectedMaterialAsset{};
        int m_materialPreviewWidth = 0;
        int m_materialPreviewHeight = 0;
        bool m_requestMaterialPreview = false;
        bool m_spawnWithPhysics = true;
        int m_spawnColliderType = 0;
        float m_spawnMass = 1.0f;
        float m_spawnFriction = 0.45f;
        float m_spawnBounciness = 0.05f;
        bool m_spawnUseGravity = true;
        bool m_spawnIsStatic = false;
        std::vector<std::filesystem::path> m_undoSnapshots{};
        std::vector<std::filesystem::path> m_redoSnapshots{};
        std::uint64_t m_snapshotCounter = 0;
    };
}
