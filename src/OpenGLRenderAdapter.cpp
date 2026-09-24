#include "OpenGLRenderAdapter.h"

#include "Logger.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <array>
#include <cstddef>
#include <vector>

namespace archi
{
    namespace
    {
        constexpr unsigned int kDefaultDrawMode = GL_TRIANGLES;

        constexpr const char* kDebugVertexShader = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;

            uniform mat4 uModel;
            uniform mat4 uView;
            uniform mat4 uProjection;

            void main()
            {
                gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
            }
        )";

        constexpr const char* kDebugFragmentShader = R"(
            #version 330 core
            uniform vec4 uColor;

            out vec4 FragColor;

            void main()
            {
                FragColor = uColor;
            }
        )";
    }

    std::size_t OpenGLRenderAdapter::KeyToIndex(Key key)
    {
        switch (key)
        {
        case Key::Up: return 0;
        case Key::Down: return 1;
        case Key::Left: return 2;
        case Key::Right: return 3;
        case Key::W: return 4;
        case Key::A: return 5;
        case Key::S: return 6;
        case Key::D: return 7;
        case Key::Q: return 8;
        case Key::E: return 9;
        case Key::Enter: return 10;
        case Key::Escape: return 11;
        case Key::P: return 12;
        case Key::LeftShift: return 13;
        case Key::N: return 14;
        case Key::R: return 15;
        case Key::K: return 16;
        case Key::L: return 17;
        case Key::M: return 18;
        case Key::Space: return 19;
        case Key::F3: return 20;
        default: return static_cast<std::size_t>(-1);
        }
    }

    std::size_t OpenGLRenderAdapter::MouseToIndex(MouseButton button)
    {
        switch (button)
        {
        case MouseButton::Left: return 0;
        case MouseButton::Right: return 1;
        default: return static_cast<std::size_t>(-1);
        }
    }

    OpenGLRenderAdapter::~OpenGLRenderAdapter()
    {
        Shutdown();
    }

    void OpenGLRenderAdapter::OnKeyEvent(int glfwKey, int action)
    {
        auto setDown = [&](Key key) {
            const std::size_t index = KeyToIndex(key);
            if (index < m_keyDown.size())
                m_keyDown[index] = (action != GLFW_RELEASE) ? 1 : 0;
        };

        switch (glfwKey)
        {
        case GLFW_KEY_UP: setDown(Key::Up); break;
        case GLFW_KEY_DOWN: setDown(Key::Down); break;
        case GLFW_KEY_LEFT: setDown(Key::Left); break;
        case GLFW_KEY_RIGHT: setDown(Key::Right); break;
        case GLFW_KEY_W: setDown(Key::W); break;
        case GLFW_KEY_A: setDown(Key::A); break;
        case GLFW_KEY_S: setDown(Key::S); break;
        case GLFW_KEY_D: setDown(Key::D); break;
        case GLFW_KEY_Q: setDown(Key::Q); break;
        case GLFW_KEY_E: setDown(Key::E); break;
        case GLFW_KEY_ENTER: setDown(Key::Enter); break;
        case GLFW_KEY_ESCAPE: setDown(Key::Escape); break;
        case GLFW_KEY_P: setDown(Key::P); break;
        case GLFW_KEY_LEFT_SHIFT: setDown(Key::LeftShift); break;
        case GLFW_KEY_N: setDown(Key::N); break;
        case GLFW_KEY_R: setDown(Key::R); break;
        case GLFW_KEY_K: setDown(Key::K); break;
        case GLFW_KEY_L: setDown(Key::L); break;
        case GLFW_KEY_M: setDown(Key::M); break;
        case GLFW_KEY_SPACE: setDown(Key::Space); break;
        case GLFW_KEY_F3: setDown(Key::F3); break;
        default: break;
        }
    }

    void OpenGLRenderAdapter::OnMouseButtonEvent(int glfwButton, int action)
    {
        auto setDown = [&](MouseButton button) {
            const std::size_t index = MouseToIndex(button);
            if (index < m_mouseDown.size())
                m_mouseDown[index] = (action != GLFW_RELEASE) ? 1 : 0;
        };

        switch (glfwButton)
        {
        case GLFW_MOUSE_BUTTON_LEFT: setDown(MouseButton::Left); break;
        case GLFW_MOUSE_BUTTON_RIGHT: setDown(MouseButton::Right); break;
        default: break;
        }
    }

    void OpenGLRenderAdapter::OnCursorPositionEvent(double xpos, double ypos)
    {
        m_mousePosition = { static_cast<float>(xpos), static_cast<float>(ypos) };
    }

    void OpenGLRenderAdapter::OnScrollEvent(double yoffset)
    {
        m_scrollYAccum += static_cast<float>(yoffset);
    }

    bool OpenGLRenderAdapter::Init(const RenderConfig& cfg)
    {
        m_cfg = cfg;

        if (!glfwInit())
        {
            Logger::Error("GLFW init failed");
            return false;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

        GLFWwindow* primary = glfwCreateWindow(cfg.width, cfg.height, cfg.title.c_str(), nullptr, nullptr);
        if (!primary)
        {
            Logger::Error("GLFW window creation failed");
            glfwTerminate();
            return false;
        }

        m_windows.push_back(WindowData{ primary, 0.08f, 0.08f, 0.10f });

        glfwMakeContextCurrent(primary);
        glfwSwapInterval(cfg.vsync ? 1 : 0);

        glfwSetWindowUserPointer(primary, this);
        glfwSetKeyCallback(primary, [](GLFWwindow* window, int key, int, int action, int) {
            auto* self = static_cast<OpenGLRenderAdapter*>(glfwGetWindowUserPointer(window));
            if (self)
                self->OnKeyEvent(key, action);
        });
        glfwSetMouseButtonCallback(primary, [](GLFWwindow* window, int button, int action, int) {
            auto* self = static_cast<OpenGLRenderAdapter*>(glfwGetWindowUserPointer(window));
            if (self)
                self->OnMouseButtonEvent(button, action);
        });
        glfwSetCursorPosCallback(primary, [](GLFWwindow* window, double xpos, double ypos) {
            auto* self = static_cast<OpenGLRenderAdapter*>(glfwGetWindowUserPointer(window));
            if (self)
                self->OnCursorPositionEvent(xpos, ypos);
        });
        glfwSetScrollCallback(primary, [](GLFWwindow* window, double, double yoffset) {
            auto* self = static_cast<OpenGLRenderAdapter*>(glfwGetWindowUserPointer(window));
            if (self)
                self->OnScrollEvent(yoffset);
        });

        glfwSetInputMode(primary, GLFW_STICKY_KEYS, GLFW_TRUE);
        glfwSetInputMode(primary, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);

        if (!gladLoadGL())
        {
            Logger::Error("Can't load GLAD");
            return false;
        }

        Logger::Info("OpenGL ", GLVersion.major, ".", GLVersion.minor);

        int fbw = 0;
        int fbh = 0;
        glfwGetFramebufferSize(primary, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        return InitGLObjects();
    }

    void OpenGLRenderAdapter::Shutdown()
    {
        if (m_windows.empty() && m_meshes.empty() && m_textures.empty() && m_shaders.empty())
            return;

        for (auto& window : m_windows)
        {
            if (!window.window)
                continue;

            glfwMakeContextCurrent(window.window);
            DestroyWindowVaos(window);
        }

        DestroyGLObjects();

        for (auto& window : m_windows)
        {
            if (window.window)
                glfwDestroyWindow(window.window);
        }
        m_windows.clear();

        glfwTerminate();
    }

    void OpenGLRenderAdapter::BeginFrame()
    {
        for (std::size_t index = 1; index < m_windows.size();)
        {
            if (m_windows[index].window && glfwWindowShouldClose(m_windows[index].window))
            {
                glfwMakeContextCurrent(m_windows[index].window);
                DestroyWindowVaos(m_windows[index]);
                glfwDestroyWindow(m_windows[index].window);
                m_windows.erase(m_windows.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }
            ++index;
        }

        for (auto& window : m_windows)
        {
            if (!window.window)
                continue;

            glfwMakeContextCurrent(window.window);
            int fbw = 0;
            int fbh = 0;
            glfwGetFramebufferSize(window.window, &fbw, &fbh);
            glViewport(0, 0, fbw, fbh);
            glEnable(GL_DEPTH_TEST);
            glClearColor(window.clearR, window.clearG, window.clearB, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
    }

    MeshHandle OpenGLRenderAdapter::UploadMesh(const MeshData& mesh)
    {
        MeshResource resource{};
        if (!BuildMeshResource(mesh, resource))
            return InvalidMeshHandle;

        const MeshHandle handle = m_nextMeshHandle++;
        m_meshes.emplace(handle, resource);

        for (auto& window : m_windows)
        {
            if (!window.window)
                continue;

            glfwMakeContextCurrent(window.window);
            window.vaos[handle] = CreateVaoForCurrentContext(resource);
        }

        return handle;
    }

    bool OpenGLRenderAdapter::ReloadMesh(MeshHandle handle, const MeshData& mesh)
    {
        auto it = m_meshes.find(handle);
        if (it == m_meshes.end())
            return false;

        MeshResource next{};
        if (!BuildMeshResource(mesh, next))
            return false;

        DestroyMeshVaos(handle);

        if (it->second.ebo)
            glDeleteBuffers(1, &it->second.ebo);
        if (it->second.vbo)
            glDeleteBuffers(1, &it->second.vbo);

        it->second = next;

        for (auto& window : m_windows)
        {
            if (!window.window)
                continue;

            glfwMakeContextCurrent(window.window);
            window.vaos[handle] = CreateVaoForCurrentContext(next);
        }

        return true;
    }

    TextureHandle OpenGLRenderAdapter::CreateTexture(const TextureData& texture)
    {
        if (texture.width <= 0 || texture.height <= 0 || texture.pixels.empty())
            return InvalidTextureHandle;

        TextureResource resource{};
        resource.width = texture.width;
        resource.height = texture.height;

        glGenTextures(1, &resource.glId);
        glBindTexture(GL_TEXTURE_2D, resource.glId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            texture.width,
            texture.height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            texture.pixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);

        const TextureHandle handle = m_nextTextureHandle++;
        m_textures.emplace(handle, resource);
        return handle;
    }

    bool OpenGLRenderAdapter::ReloadTexture(TextureHandle handle, const TextureData& texture)
    {
        auto it = m_textures.find(handle);
        if (it == m_textures.end())
            return false;
        if (texture.width <= 0 || texture.height <= 0 || texture.pixels.empty())
            return false;

        if (it->second.glId == 0)
            glGenTextures(1, &it->second.glId);

        glBindTexture(GL_TEXTURE_2D, it->second.glId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            texture.width,
            texture.height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            texture.pixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);

        it->second.width = texture.width;
        it->second.height = texture.height;
        return true;
    }

    ShaderHandle OpenGLRenderAdapter::CreateShaderProgram(const ShaderSource& shaderSource)
    {
        const ShaderResource resource = BuildShaderResource(shaderSource);
        if (resource.program == 0)
            return InvalidShaderHandle;

        const ShaderHandle handle = m_nextShaderHandle++;
        m_shaders.emplace(handle, resource);
        return handle;
    }

    bool OpenGLRenderAdapter::ReloadShaderProgram(ShaderHandle handle, const ShaderSource& shaderSource)
    {
        auto it = m_shaders.find(handle);
        if (it == m_shaders.end())
            return false;

        ShaderResource next = BuildShaderResource(shaderSource);
        if (next.program == 0)
            return false;

        if (it->second.program)
            glDeleteProgram(it->second.program);
        it->second = next;
        return true;
    }

    void OpenGLRenderAdapter::DrawMesh(const RenderMeshCommand& command)
    {
        const auto meshIt = m_meshes.find(command.mesh);
        const auto shaderIt = m_shaders.find(command.shader);
        if (meshIt == m_meshes.end() || shaderIt == m_shaders.end())
            return;

        const MeshResource& mesh = meshIt->second;
        const ShaderResource& shader = shaderIt->second;
        const TextureResource* texture = nullptr;
        if (command.useTexture && command.texture != InvalidTextureHandle)
        {
            const auto textureIt = m_textures.find(command.texture);
            if (textureIt != m_textures.end())
                texture = &textureIt->second;
        }

        if (m_viewportTarget.active || m_materialPreviewTarget.active)
        {
            if (m_windows.empty() || !m_windows[0].window)
                return;

            glfwMakeContextCurrent(m_windows[0].window);
            WindowData& window = m_windows[0];
            const unsigned int vao = GetOrCreateVao(window, command.mesh, mesh);
            if (vao == 0)
                return;

            glUseProgram(shader.program);
            glUniformMatrix4fv(shader.uModelLoc, 1, GL_FALSE, command.model.data());
            glUniformMatrix4fv(shader.uViewLoc, 1, GL_FALSE, command.view.data());
            glUniformMatrix4fv(shader.uProjectionLoc, 1, GL_FALSE, command.projection.data());
            glUniform4f(shader.uColorLoc, command.color.x, command.color.y, command.color.z, command.color.w);
            glUniform1i(shader.uUseTextureLoc, texture ? 1 : 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture ? texture->glId : 0);
            glUniform1i(shader.uTextureLoc, 0);

            glBindVertexArray(vao);
            if (mesh.indexed)
                glDrawElements(mesh.drawMode, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
            else
                glDrawArrays(mesh.drawMode, 0, mesh.vertexCount);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
            return;
        }

        for (auto& window : m_windows)
        {
            if (!window.window)
                continue;

            glfwMakeContextCurrent(window.window);
            const unsigned int vao = GetOrCreateVao(window, command.mesh, mesh);
            if (vao == 0)
                continue;

            glUseProgram(shader.program);
            glUniformMatrix4fv(shader.uModelLoc, 1, GL_FALSE, command.model.data());
            glUniformMatrix4fv(shader.uViewLoc, 1, GL_FALSE, command.view.data());
            glUniformMatrix4fv(shader.uProjectionLoc, 1, GL_FALSE, command.projection.data());
            glUniform4f(shader.uColorLoc, command.color.x, command.color.y, command.color.z, command.color.w);
            glUniform1i(shader.uUseTextureLoc, texture ? 1 : 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture ? texture->glId : 0);
            glUniform1i(shader.uTextureLoc, 0);

            glBindVertexArray(vao);
            if (mesh.indexed)
                glDrawElements(mesh.drawMode, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
            else
                glDrawArrays(mesh.drawMode, 0, mesh.vertexCount);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    void OpenGLRenderAdapter::DrawDebugBox(const RenderDebugBoxCommand& command)
    {
        if (m_debugBox.program == 0)
            return;

        glDisable(GL_CULL_FACE);

        if (m_viewportTarget.active || m_materialPreviewTarget.active)
        {
            if (m_windows.empty() || !m_windows[0].window || m_windows[0].debugBoxVao == 0)
            {
                glEnable(GL_CULL_FACE);
                return;
            }

            glfwMakeContextCurrent(m_windows[0].window);
            glUseProgram(m_debugBox.program);
            glUniformMatrix4fv(m_debugBox.uModelLoc, 1, GL_FALSE, command.model.data());
            glUniformMatrix4fv(m_debugBox.uViewLoc, 1, GL_FALSE, command.view.data());
            glUniformMatrix4fv(m_debugBox.uProjectionLoc, 1, GL_FALSE, command.projection.data());
            glUniform4f(m_debugBox.uColorLoc, command.color.x, command.color.y, command.color.z, command.color.w);
            glBindVertexArray(m_windows[0].debugBoxVao);
            glLineWidth(2.0f);
            glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
            glEnable(GL_CULL_FACE);
            return;
        }

        for (auto& window : m_windows)
        {
            if (!window.window || window.debugBoxVao == 0)
                continue;

            glfwMakeContextCurrent(window.window);
            glUseProgram(m_debugBox.program);
            glUniformMatrix4fv(m_debugBox.uModelLoc, 1, GL_FALSE, command.model.data());
            glUniformMatrix4fv(m_debugBox.uViewLoc, 1, GL_FALSE, command.view.data());
            glUniformMatrix4fv(m_debugBox.uProjectionLoc, 1, GL_FALSE, command.projection.data());
            glUniform4f(m_debugBox.uColorLoc, command.color.x, command.color.y, command.color.z, command.color.w);
            glBindVertexArray(window.debugBoxVao);
            glLineWidth(2.0f);
            glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
        }

        glEnable(GL_CULL_FACE);
    }

    void OpenGLRenderAdapter::DrawDebugSphere(const RenderDebugSphereCommand& command)
    {
        if (m_debugBox.program == 0 || m_debugBox.circleVertexCount <= 0)
            return;

        glDisable(GL_CULL_FACE);

        const Mat4 rotateX = MakeRotationXMatrix(1.57079632679f);
        const Mat4 rotateY = MakeRotationYMatrix(1.57079632679f);

        if (m_viewportTarget.active || m_materialPreviewTarget.active)
        {
            if (m_windows.empty() || !m_windows[0].window || m_windows[0].debugCircleVao == 0)
            {
                glEnable(GL_CULL_FACE);
                return;
            }

            glfwMakeContextCurrent(m_windows[0].window);
            glUseProgram(m_debugBox.program);
            glUniformMatrix4fv(m_debugBox.uViewLoc, 1, GL_FALSE, command.view.data());
            glUniformMatrix4fv(m_debugBox.uProjectionLoc, 1, GL_FALSE, command.projection.data());
            glUniform4f(m_debugBox.uColorLoc, command.color.x, command.color.y, command.color.z, command.color.w);
            glBindVertexArray(m_windows[0].debugCircleVao);
            glLineWidth(2.0f);

            glUniformMatrix4fv(m_debugBox.uModelLoc, 1, GL_FALSE, command.model.data());
            glDrawArrays(GL_LINE_LOOP, 0, m_debugBox.circleVertexCount);

            const Mat4 modelX = command.model * rotateX;
            glUniformMatrix4fv(m_debugBox.uModelLoc, 1, GL_FALSE, modelX.data());
            glDrawArrays(GL_LINE_LOOP, 0, m_debugBox.circleVertexCount);

            const Mat4 modelY = command.model * rotateY;
            glUniformMatrix4fv(m_debugBox.uModelLoc, 1, GL_FALSE, modelY.data());
            glDrawArrays(GL_LINE_LOOP, 0, m_debugBox.circleVertexCount);

            glBindVertexArray(0);
            glEnable(GL_CULL_FACE);
            return;
        }

        for (auto& window : m_windows)
        {
            if (!window.window || window.debugCircleVao == 0)
                continue;

            glfwMakeContextCurrent(window.window);
            glUseProgram(m_debugBox.program);
            glUniformMatrix4fv(m_debugBox.uViewLoc, 1, GL_FALSE, command.view.data());
            glUniformMatrix4fv(m_debugBox.uProjectionLoc, 1, GL_FALSE, command.projection.data());
            glUniform4f(m_debugBox.uColorLoc, command.color.x, command.color.y, command.color.z, command.color.w);
            glBindVertexArray(window.debugCircleVao);
            glLineWidth(2.0f);

            glUniformMatrix4fv(m_debugBox.uModelLoc, 1, GL_FALSE, command.model.data());
            glDrawArrays(GL_LINE_LOOP, 0, m_debugBox.circleVertexCount);

            const Mat4 modelX = command.model * rotateX;
            glUniformMatrix4fv(m_debugBox.uModelLoc, 1, GL_FALSE, modelX.data());
            glDrawArrays(GL_LINE_LOOP, 0, m_debugBox.circleVertexCount);

            const Mat4 modelY = command.model * rotateY;
            glUniformMatrix4fv(m_debugBox.uModelLoc, 1, GL_FALSE, modelY.data());
            glDrawArrays(GL_LINE_LOOP, 0, m_debugBox.circleVertexCount);

            glBindVertexArray(0);
        }

        glEnable(GL_CULL_FACE);
    }

    void OpenGLRenderAdapter::EndFrame()
    {
        for (auto& window : m_windows)
        {
            if (!window.window)
                continue;
            glfwMakeContextCurrent(window.window);
            glfwSwapBuffers(window.window);
        }
    }

    void OpenGLRenderAdapter::PollEvents()
    {
        glfwPollEvents();
    }

    bool OpenGLRenderAdapter::ShouldClose() const
    {
        if (m_windows.empty() || !m_windows[0].window)
            return true;
        return glfwWindowShouldClose(m_windows[0].window) != 0;
    }

    int OpenGLRenderAdapter::DrawableWidth() const
    {
        if (m_windows.empty() || !m_windows[0].window)
            return m_cfg.width;

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(m_windows[0].window, &width, &height);
        return width > 0 ? width : m_cfg.width;
    }

    int OpenGLRenderAdapter::DrawableHeight() const
    {
        if (m_windows.empty() || !m_windows[0].window)
            return m_cfg.height;

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(m_windows[0].window, &width, &height);
        return height > 0 ? height : m_cfg.height;
    }

    bool OpenGLRenderAdapter::IsKeyDown(Key key) const
    {
        const std::size_t index = KeyToIndex(key);
        return index < m_keyDown.size() ? (m_keyDown[index] != 0) : false;
    }

    bool OpenGLRenderAdapter::IsMouseButtonDown(MouseButton button) const
    {
        const std::size_t index = MouseToIndex(button);
        return index < m_mouseDown.size() ? (m_mouseDown[index] != 0) : false;
    }

    Vec2 OpenGLRenderAdapter::MousePosition() const
    {
        return m_mousePosition;
    }

    float OpenGLRenderAdapter::ConsumeScrollDeltaY()
    {
        const float value = m_scrollYAccum;
        m_scrollYAccum = 0.0f;
        return value;
    }

    bool OpenGLRenderAdapter::OpenAdditionalWindow(const RenderConfig& cfg, float clearR, float clearG, float clearB)
    {
        if (m_windows.empty() || !m_windows[0].window)
            return false;

        const std::string title = cfg.title + " (Window " + std::to_string(++m_windowCounter) + ")";
        GLFWwindow* sharedWith = m_windows[0].window;
        GLFWwindow* window = glfwCreateWindow(cfg.width, cfg.height, title.c_str(), nullptr, sharedWith);
        if (!window)
        {
            Logger::Error("Failed to create additional window");
            return false;
        }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(cfg.vsync ? 1 : 0);

        glfwSetWindowUserPointer(window, this);
        glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int) {
            auto* self = static_cast<OpenGLRenderAdapter*>(glfwGetWindowUserPointer(w));
            if (self)
                self->OnKeyEvent(key, action);
        });
        glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int) {
            auto* self = static_cast<OpenGLRenderAdapter*>(glfwGetWindowUserPointer(w));
            if (self)
                self->OnMouseButtonEvent(button, action);
        });
        glfwSetCursorPosCallback(window, [](GLFWwindow* w, double xpos, double ypos) {
            auto* self = static_cast<OpenGLRenderAdapter*>(glfwGetWindowUserPointer(w));
            if (self)
                self->OnCursorPositionEvent(xpos, ypos);
        });
        glfwSetScrollCallback(window, [](GLFWwindow* w, double, double yoffset) {
            auto* self = static_cast<OpenGLRenderAdapter*>(glfwGetWindowUserPointer(w));
            if (self)
                self->OnScrollEvent(yoffset);
        });

        glfwSetInputMode(window, GLFW_STICKY_KEYS, GLFW_TRUE);
        glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);
        glEnable(GL_DEPTH_TEST);

        WindowData data{};
        data.window = window;
        data.clearR = clearR;
        data.clearG = clearG;
        data.clearB = clearB;
        for (const auto& [meshHandle, mesh] : m_meshes)
            data.vaos[meshHandle] = CreateVaoForCurrentContext(mesh);
        if (m_debugBox.boxVbo != 0 && m_debugBox.boxEbo != 0)
            data.debugBoxVao = CreateDebugVaoForCurrentContext();
        if (m_debugBox.circleVbo != 0)
            data.debugCircleVao = CreateDebugCircleVaoForCurrentContext();

        m_windows.push_back(std::move(data));
        Logger::Info("Additional window created. Total windows: ", m_windows.size());
        return true;
    }

    void* OpenGLRenderAdapter::PrimaryWindowHandle() const
    {
        return (!m_windows.empty() && m_windows[0].window) ? m_windows[0].window : nullptr;
    }

    bool OpenGLRenderAdapter::BeginViewportRender(int width, int height)
    {
        if (m_windows.empty() || !m_windows[0].window || width <= 0 || height <= 0)
            return false;

        glfwMakeContextCurrent(m_windows[0].window);
        if (!EnsureViewportRenderTarget(width, height))
            return false;

        glBindFramebuffer(GL_FRAMEBUFFER, m_viewportTarget.fbo);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_viewportTarget.active = true;
        return true;
    }

    void OpenGLRenderAdapter::EndViewportRender()
    {
        if (!m_viewportTarget.active)
            return;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!m_windows.empty() && m_windows[0].window)
        {
            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(m_windows[0].window, &width, &height);
            glViewport(0, 0, width, height);
        }
        m_viewportTarget.active = false;
    }

    std::uintptr_t OpenGLRenderAdapter::ViewportTextureHandle() const
    {
        return static_cast<std::uintptr_t>(m_viewportTarget.colorTexture);
    }

    bool OpenGLRenderAdapter::BeginMaterialPreviewRender(int width, int height)
    {
        if (m_windows.empty() || !m_windows[0].window || width <= 0 || height <= 0)
            return false;

        glfwMakeContextCurrent(m_windows[0].window);
        if (!EnsureRenderTarget(m_materialPreviewTarget, width, height))
            return false;

        glBindFramebuffer(GL_FRAMEBUFFER, m_materialPreviewTarget.fbo);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_materialPreviewTarget.active = true;
        return true;
    }

    void OpenGLRenderAdapter::EndMaterialPreviewRender()
    {
        if (!m_materialPreviewTarget.active)
            return;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!m_windows.empty() && m_windows[0].window)
        {
            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(m_windows[0].window, &width, &height);
            glViewport(0, 0, width, height);
        }
        m_materialPreviewTarget.active = false;
    }

    std::uintptr_t OpenGLRenderAdapter::MaterialPreviewTextureHandle() const
    {
        return static_cast<std::uintptr_t>(m_materialPreviewTarget.colorTexture);
    }

    bool OpenGLRenderAdapter::InitGLObjects()
    {
        return InitDebugObjects();
    }

    void OpenGLRenderAdapter::DestroyGLObjects()
    {
        auto destroyRenderTarget = [](ViewportRenderTarget& target) {
            if (target.fbo)
            {
                glDeleteFramebuffers(1, &target.fbo);
                target.fbo = 0;
            }
            if (target.colorTexture)
            {
                glDeleteTextures(1, &target.colorTexture);
                target.colorTexture = 0;
            }
            if (target.depthStencilRbo)
            {
                glDeleteRenderbuffers(1, &target.depthStencilRbo);
                target.depthStencilRbo = 0;
            }
            target.width = 0;
            target.height = 0;
            target.active = false;
        };

        destroyRenderTarget(m_viewportTarget);
        destroyRenderTarget(m_materialPreviewTarget);

        DestroyDebugObjects();

        for (auto& [_, shader] : m_shaders)
        {
            if (shader.program)
                glDeleteProgram(shader.program);
        }
        m_shaders.clear();

        for (auto& [_, texture] : m_textures)
        {
            if (texture.glId)
                glDeleteTextures(1, &texture.glId);
        }
        m_textures.clear();

        for (auto& [_, mesh] : m_meshes)
        {
            if (mesh.ebo)
                glDeleteBuffers(1, &mesh.ebo);
            if (mesh.vbo)
                glDeleteBuffers(1, &mesh.vbo);
        }
        m_meshes.clear();
    }

    bool OpenGLRenderAdapter::EnsureViewportRenderTarget(int width, int height)
    {
        return EnsureRenderTarget(m_viewportTarget, width, height);
    }

    bool OpenGLRenderAdapter::EnsureRenderTarget(ViewportRenderTarget& target, int width, int height)
    {
        if (width <= 0 || height <= 0)
            return false;

        if (target.fbo != 0 &&
            target.width == width &&
            target.height == height)
        {
            return true;
        }

        if (target.fbo)
        {
            glDeleteFramebuffers(1, &target.fbo);
            target.fbo = 0;
        }
        if (target.colorTexture)
        {
            glDeleteTextures(1, &target.colorTexture);
            target.colorTexture = 0;
        }
        if (target.depthStencilRbo)
        {
            glDeleteRenderbuffers(1, &target.depthStencilRbo);
            target.depthStencilRbo = 0;
        }

        glGenFramebuffers(1, &target.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);

        glGenTextures(1, &target.colorTexture);
        glBindTexture(GL_TEXTURE_2D, target.colorTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.colorTexture, 0);

        glGenRenderbuffers(1, &target.depthStencilRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, target.depthStencilRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER,
            GL_DEPTH_STENCIL_ATTACHMENT,
            GL_RENDERBUFFER,
            target.depthStencilRbo);

        const bool complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!complete)
        {
            Logger::Error("Viewport framebuffer creation failed");
            return false;
        }

        target.width = width;
        target.height = height;
        target.active = false;
        return true;
    }

    bool OpenGLRenderAdapter::InitDebugObjects()
    {
        const unsigned int vs = CompileShader(GL_VERTEX_SHADER, kDebugVertexShader);
        if (!vs)
            return false;

        const unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, kDebugFragmentShader);
        if (!fs)
        {
            glDeleteShader(vs);
            return false;
        }

        m_debugBox.program = LinkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!m_debugBox.program)
            return false;

        m_debugBox.uModelLoc = glGetUniformLocation(m_debugBox.program, "uModel");
        m_debugBox.uViewLoc = glGetUniformLocation(m_debugBox.program, "uView");
        m_debugBox.uProjectionLoc = glGetUniformLocation(m_debugBox.program, "uProjection");
        m_debugBox.uColorLoc = glGetUniformLocation(m_debugBox.program, "uColor");

        constexpr std::array<float, 24> boxVertices = {
            -0.5f, -0.5f, -0.5f,
             0.5f, -0.5f, -0.5f,
             0.5f,  0.5f, -0.5f,
            -0.5f,  0.5f, -0.5f,
            -0.5f, -0.5f,  0.5f,
             0.5f, -0.5f,  0.5f,
             0.5f,  0.5f,  0.5f,
            -0.5f,  0.5f,  0.5f
        };

        constexpr std::array<unsigned int, 24> boxIndices = {
            0, 1, 1, 2, 2, 3, 3, 0,
            4, 5, 5, 6, 6, 7, 7, 4,
            0, 4, 1, 5, 2, 6, 3, 7
        };

        glGenBuffers(1, &m_debugBox.boxVbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_debugBox.boxVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(boxVertices.size() * sizeof(float)), boxVertices.data(), GL_STATIC_DRAW);

        glGenBuffers(1, &m_debugBox.boxEbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_debugBox.boxEbo);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(boxIndices.size() * sizeof(unsigned int)),
            boxIndices.data(),
            GL_STATIC_DRAW);

        constexpr int kCircleSegments = 48;
        std::vector<float> circleVertices{};
        circleVertices.reserve(static_cast<std::size_t>(kCircleSegments) * 3u);
        for (int segment = 0; segment < kCircleSegments; ++segment)
        {
            const float angle = (6.28318530718f * static_cast<float>(segment)) / static_cast<float>(kCircleSegments);
            circleVertices.push_back(std::cos(angle) * 0.5f);
            circleVertices.push_back(std::sin(angle) * 0.5f);
            circleVertices.push_back(0.0f);
        }

        glGenBuffers(1, &m_debugBox.circleVbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_debugBox.circleVbo);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(circleVertices.size() * sizeof(float)),
            circleVertices.data(),
            GL_STATIC_DRAW);
        m_debugBox.circleVertexCount = kCircleSegments;

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        for (auto& window : m_windows)
        {
            if (!window.window)
                continue;

            glfwMakeContextCurrent(window.window);
            window.debugBoxVao = CreateDebugVaoForCurrentContext();
            window.debugCircleVao = CreateDebugCircleVaoForCurrentContext();
        }

        return true;
    }

    void OpenGLRenderAdapter::DestroyDebugObjects()
    {
        for (auto& window : m_windows)
        {
            if (window.debugBoxVao)
            {
                glDeleteVertexArrays(1, &window.debugBoxVao);
                window.debugBoxVao = 0;
            }
            if (window.debugCircleVao)
            {
                glDeleteVertexArrays(1, &window.debugCircleVao);
                window.debugCircleVao = 0;
            }
        }

        if (m_debugBox.boxEbo)
            glDeleteBuffers(1, &m_debugBox.boxEbo);
        if (m_debugBox.boxVbo)
            glDeleteBuffers(1, &m_debugBox.boxVbo);
        if (m_debugBox.circleVbo)
            glDeleteBuffers(1, &m_debugBox.circleVbo);
        if (m_debugBox.program)
            glDeleteProgram(m_debugBox.program);

        m_debugBox = {};
    }

    unsigned int OpenGLRenderAdapter::CreateVaoForCurrentContext(const MeshResource& mesh)
    {
        unsigned int vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        if (mesh.indexed)
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        if (!mesh.indexed)
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        return vao;
    }

    unsigned int OpenGLRenderAdapter::GetOrCreateVao(WindowData& window, MeshHandle meshHandle, const MeshResource& mesh)
    {
        const auto it = window.vaos.find(meshHandle);
        if (it != window.vaos.end())
            return it->second;

        const unsigned int vao = CreateVaoForCurrentContext(mesh);
        window.vaos.emplace(meshHandle, vao);
        return vao;
    }

    unsigned int OpenGLRenderAdapter::CreateDebugVaoForCurrentContext()
    {
        unsigned int vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_debugBox.boxVbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_debugBox.boxEbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        return vao;
    }

    unsigned int OpenGLRenderAdapter::CreateDebugCircleVaoForCurrentContext()
    {
        unsigned int vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_debugBox.circleVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return vao;
    }

    void OpenGLRenderAdapter::DestroyWindowVaos(WindowData& window)
    {
        for (auto& [_, vao] : window.vaos)
        {
            if (vao)
                glDeleteVertexArrays(1, &vao);
        }
        window.vaos.clear();

        if (window.debugBoxVao)
        {
            glDeleteVertexArrays(1, &window.debugBoxVao);
            window.debugBoxVao = 0;
        }

        if (window.debugCircleVao)
        {
            glDeleteVertexArrays(1, &window.debugCircleVao);
            window.debugCircleVao = 0;
        }
    }

    void OpenGLRenderAdapter::DestroyMeshVaos(MeshHandle meshHandle)
    {
        for (auto& window : m_windows)
        {
            const auto it = window.vaos.find(meshHandle);
            if (it == window.vaos.end())
                continue;

            if (it->second)
                glDeleteVertexArrays(1, &it->second);
            window.vaos.erase(it);
        }
    }

    unsigned int OpenGLRenderAdapter::CompileShader(unsigned int type, const char* src)
    {
        const unsigned int shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        int ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            int len = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> buffer(static_cast<std::size_t>(len) + 1);
            glGetShaderInfoLog(shader, len, nullptr, buffer.data());
            Logger::Error("Shader compile failed: ", buffer.data());
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    unsigned int OpenGLRenderAdapter::LinkProgram(unsigned int vs, unsigned int fs)
    {
        const unsigned int program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        int ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            int len = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> buffer(static_cast<std::size_t>(len) + 1);
            glGetProgramInfoLog(program, len, nullptr, buffer.data());
            Logger::Error("Program link failed: ", buffer.data());
            glDeleteProgram(program);
            return 0;
        }

        return program;
    }

    OpenGLRenderAdapter::ShaderResource OpenGLRenderAdapter::BuildShaderResource(const ShaderSource& shaderSource)
    {
        ShaderResource resource{};

        const unsigned int vs = CompileShader(GL_VERTEX_SHADER, shaderSource.vertexCode.c_str());
        if (!vs)
            return resource;

        const unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, shaderSource.fragmentCode.c_str());
        if (!fs)
        {
            glDeleteShader(vs);
            return resource;
        }

        resource.program = LinkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!resource.program)
            return resource;

        resource.uModelLoc = glGetUniformLocation(resource.program, "uModel");
        resource.uViewLoc = glGetUniformLocation(resource.program, "uView");
        resource.uProjectionLoc = glGetUniformLocation(resource.program, "uProjection");
        resource.uColorLoc = glGetUniformLocation(resource.program, "uColor");
        resource.uUseTextureLoc = glGetUniformLocation(resource.program, "uUseTexture");
        resource.uTextureLoc = glGetUniformLocation(resource.program, "uTexture");
        return resource;
    }

    bool OpenGLRenderAdapter::BuildMeshResource(const MeshData& mesh, MeshResource& outResource)
    {
        if (mesh.vertices.empty())
            return false;

        MeshResource resource{};
        resource.drawMode = kDefaultDrawMode;
        resource.vertexCount = static_cast<int>(mesh.vertices.size());
        resource.indexCount = static_cast<int>(mesh.indices.size());
        resource.indexed = !mesh.indices.empty();

        glGenBuffers(1, &resource.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, resource.vbo);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Vertex)),
            mesh.vertices.data(),
            GL_STATIC_DRAW);

        if (resource.indexed)
        {
            glGenBuffers(1, &resource.ebo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resource.ebo);
            glBufferData(
                GL_ELEMENT_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
                mesh.indices.data(),
                GL_STATIC_DRAW);
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        outResource = resource;
        return true;
    }
}
