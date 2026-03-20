#include "OpenGLRenderAdapter.h"

#include "Logger.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstddef>
#include <vector>

namespace archi
{
    namespace
    {
        constexpr unsigned int kDefaultDrawMode = GL_TRIANGLES;
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
        for (std::size_t i = 1; i < m_windows.size();)
        {
            if (m_windows[i].window && glfwWindowShouldClose(m_windows[i].window))
            {
                glfwMakeContextCurrent(m_windows[i].window);
                DestroyWindowVaos(m_windows[i]);
                glfwDestroyWindow(m_windows[i].window);
                m_windows.erase(m_windows.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            ++i;
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

        m_windows.push_back(std::move(data));
        Logger::Info("Additional window created. Total windows: ", m_windows.size());
        return true;
    }

    bool OpenGLRenderAdapter::InitGLObjects()
    {
        return true;
    }

    void OpenGLRenderAdapter::DestroyGLObjects()
    {
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

    void OpenGLRenderAdapter::DestroyWindowVaos(WindowData& window)
    {
        for (auto& [_, vao] : window.vaos)
        {
            if (vao)
                glDeleteVertexArrays(1, &vao);
        }
        window.vaos.clear();
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
