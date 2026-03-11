#include "OpenGLRenderAdapter.h"

#include "AssetPaths.h"
#include "Logger.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace archi
{
    namespace
    {
        using MeshBuffer = OpenGLRenderAdapter::MeshBuffer;

        unsigned int CreateMeshVbo(const float* vertices, std::size_t floatCount)
        {
            unsigned int vbo = 0;
            glGenBuffers(1, &vbo);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(floatCount * sizeof(float)), vertices, GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            return vbo;
        }

        unsigned int CreateVaoForCurrentContext(const MeshBuffer& mesh)
        {
            unsigned int vao = 0;
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            return vao;
        }

        MeshBuffer MakeMeshBuffer(const float* vertices, std::size_t floatCount, unsigned int drawMode)
        {
            MeshBuffer mesh{};
            mesh.vbo = CreateMeshVbo(vertices, floatCount);
            mesh.vertexCount = static_cast<int>(floatCount / 5);
            mesh.drawMode = drawMode;
            return mesh;
        }

        bool ReadBinaryFile(const std::filesystem::path& path, std::vector<unsigned char>& outData)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return false;

            file.seekg(0, std::ios::end);
            const auto size = file.tellg();
            file.seekg(0, std::ios::beg);
            if (size <= 0)
                return false;

            outData.resize(static_cast<std::size_t>(size));
            file.read(reinterpret_cast<char*>(outData.data()), size);
            return file.good();
        }

        bool ReadNextPpmToken(const std::vector<unsigned char>& data, std::size_t& pos, std::string& outToken)
        {
            while (pos < data.size())
            {
                const unsigned char ch = data[pos];
                if (std::isspace(ch))
                {
                    ++pos;
                    continue;
                }
                if (ch == '#')
                {
                    while (pos < data.size() && data[pos] != '\n')
                        ++pos;
                    continue;
                }
                break;
            }

            if (pos >= data.size())
                return false;

            outToken.clear();
            while (pos < data.size())
            {
                const unsigned char ch = data[pos];
                if (std::isspace(ch) || ch == '#')
                    break;
                outToken.push_back(static_cast<char>(ch));
                ++pos;
            }
            return !outToken.empty();
        }

        bool LoadPpmTextureFile(const std::filesystem::path& path, int& outWidth, int& outHeight, std::vector<unsigned char>& outPixels)
        {
            std::vector<unsigned char> fileData;
            if (!ReadBinaryFile(path, fileData))
                return false;

            std::size_t pos = 0;
            std::string magic;
            std::string widthText;
            std::string heightText;
            std::string maxValueText;
            if (!ReadNextPpmToken(fileData, pos, magic) ||
                !ReadNextPpmToken(fileData, pos, widthText) ||
                !ReadNextPpmToken(fileData, pos, heightText) ||
                !ReadNextPpmToken(fileData, pos, maxValueText))
            {
                return false;
            }

            const int width = std::stoi(widthText);
            const int height = std::stoi(heightText);
            const int maxValue = std::stoi(maxValueText);
            if (width <= 0 || height <= 0 || maxValue <= 0 || maxValue > 255)
                return false;

            outWidth = width;
            outHeight = height;
            outPixels.clear();
            outPixels.reserve(static_cast<std::size_t>(width * height * 4));

            if (magic == "P3")
            {
                for (int i = 0; i < width * height; ++i)
                {
                    std::string rText;
                    std::string gText;
                    std::string bText;
                    if (!ReadNextPpmToken(fileData, pos, rText) ||
                        !ReadNextPpmToken(fileData, pos, gText) ||
                        !ReadNextPpmToken(fileData, pos, bText))
                    {
                        return false;
                    }
                    outPixels.push_back(static_cast<unsigned char>(std::stoi(rText)));
                    outPixels.push_back(static_cast<unsigned char>(std::stoi(gText)));
                    outPixels.push_back(static_cast<unsigned char>(std::stoi(bText)));
                    outPixels.push_back(255);
                }
                return true;
            }

            return false;
        }
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

    std::size_t OpenGLRenderAdapter::PrimitiveToIndex(PrimitiveType primitive)
    {
        switch (primitive)
        {
        case PrimitiveType::Line: return 0;
        case PrimitiveType::Triangle: return 1;
        case PrimitiveType::Quad: return 2;
        case PrimitiveType::Cube: return 3;
        default: return 1;
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

        return InitGLObjects();
    }

    void OpenGLRenderAdapter::Shutdown()
    {
        if (m_windows.empty() && m_program == 0 && m_textureCache.empty())
            return;

        for (auto& window : m_windows)
        {
            if (!window.window)
                continue;

            glfwMakeContextCurrent(window.window);
            for (auto& vao : window.vaos)
            {
                if (vao)
                {
                    glDeleteVertexArrays(1, &vao);
                    vao = 0;
                }
            }
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
        UpdateShaderHotReload();

        for (std::size_t i = 1; i < m_windows.size();)
        {
            if (m_windows[i].window && glfwWindowShouldClose(m_windows[i].window))
            {
                glfwMakeContextCurrent(m_windows[i].window);
                for (auto& vao : m_windows[i].vaos)
                {
                    if (vao)
                    {
                        glDeleteVertexArrays(1, &vao);
                        vao = 0;
                    }
                }
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

    void OpenGLRenderAdapter::DrawPrimitive(const RenderPrimitiveCommand& command)
    {
        const std::size_t primitiveIndex = PrimitiveToIndex(command.primitive);
        const MeshBuffer& mesh = m_meshes[primitiveIndex];
        if (mesh.vbo == 0 || mesh.vertexCount == 0 || m_program == 0)
            return;

        const unsigned int textureId = ResolveTexture(command.texturePath);
        const bool useTexture = textureId != 0;

        for (auto& window : m_windows)
        {
            if (!window.window)
                continue;

            glfwMakeContextCurrent(window.window);
            glUseProgram(m_program);
            glUniformMatrix4fv(m_uModelLoc, 1, GL_FALSE, command.model.data());
            glUniformMatrix4fv(m_uViewLoc, 1, GL_FALSE, command.view.data());
            glUniformMatrix4fv(m_uProjectionLoc, 1, GL_FALSE, command.projection.data());
            glUniform4f(m_uColorLoc, command.color.x, command.color.y, command.color.z, command.color.w);
            glUniform1i(m_uUseTextureLoc, useTexture ? 1 : 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, useTexture ? textureId : 0);
            glUniform1i(m_uTextureLoc, 0);

            if (command.primitive == PrimitiveType::Line)
                glLineWidth(3.0f);

            glBindVertexArray(window.vaos[primitiveIndex]);
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

    bool OpenGLRenderAdapter::InitGLObjects()
    {
        const float lineVerts[] = {
            -0.5f, 0.0f, 0.0f, 0.0f, 0.5f,
             0.5f, 0.0f, 0.0f, 1.0f, 0.5f
        };

        const float triangleVerts[] = {
            -0.5f, -0.4f, 0.0f, 0.0f, 0.0f,
             0.5f, -0.4f, 0.0f, 1.0f, 0.0f,
             0.0f,  0.6f, 0.0f, 0.5f, 1.0f
        };

        const float quadVerts[] = {
            -0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
             0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
             0.5f,  0.5f, 0.0f, 1.0f, 1.0f,
            -0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
             0.5f,  0.5f, 0.0f, 1.0f, 1.0f,
            -0.5f,  0.5f, 0.0f, 0.0f, 1.0f
        };

        const float cubeVerts[] = {
            -0.5f, -0.5f,  0.5f, 0.0f, 0.0f,  0.5f, -0.5f,  0.5f, 1.0f, 0.0f,  0.5f,  0.5f,  0.5f, 1.0f, 1.0f,
            -0.5f, -0.5f,  0.5f, 0.0f, 0.0f,  0.5f,  0.5f,  0.5f, 1.0f, 1.0f, -0.5f,  0.5f,  0.5f, 0.0f, 1.0f,
            -0.5f, -0.5f, -0.5f, 1.0f, 0.0f, -0.5f,  0.5f, -0.5f, 1.0f, 1.0f,  0.5f,  0.5f, -0.5f, 0.0f, 1.0f,
            -0.5f, -0.5f, -0.5f, 1.0f, 0.0f,  0.5f,  0.5f, -0.5f, 0.0f, 1.0f,  0.5f, -0.5f, -0.5f, 0.0f, 0.0f,
            -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -0.5f, -0.5f,  0.5f, 1.0f, 0.0f, -0.5f,  0.5f,  0.5f, 1.0f, 1.0f,
            -0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -0.5f,  0.5f,  0.5f, 1.0f, 1.0f, -0.5f,  0.5f, -0.5f, 0.0f, 1.0f,
             0.5f, -0.5f, -0.5f, 1.0f, 0.0f,  0.5f,  0.5f, -0.5f, 1.0f, 1.0f,  0.5f,  0.5f,  0.5f, 0.0f, 1.0f,
             0.5f, -0.5f, -0.5f, 1.0f, 0.0f,  0.5f,  0.5f,  0.5f, 0.0f, 1.0f,  0.5f, -0.5f,  0.5f, 0.0f, 0.0f,
            -0.5f,  0.5f, -0.5f, 0.0f, 1.0f, -0.5f,  0.5f,  0.5f, 0.0f, 0.0f,  0.5f,  0.5f,  0.5f, 1.0f, 0.0f,
            -0.5f,  0.5f, -0.5f, 0.0f, 1.0f,  0.5f,  0.5f,  0.5f, 1.0f, 0.0f,  0.5f,  0.5f, -0.5f, 1.0f, 1.0f,
            -0.5f, -0.5f, -0.5f, 1.0f, 1.0f,  0.5f, -0.5f, -0.5f, 0.0f, 1.0f,  0.5f, -0.5f,  0.5f, 0.0f, 0.0f,
            -0.5f, -0.5f, -0.5f, 1.0f, 1.0f,  0.5f, -0.5f,  0.5f, 0.0f, 0.0f, -0.5f, -0.5f,  0.5f, 1.0f, 0.0f
        };

        m_meshes[PrimitiveToIndex(PrimitiveType::Line)] = MakeMeshBuffer(lineVerts, std::size(lineVerts), GL_LINES);
        m_meshes[PrimitiveToIndex(PrimitiveType::Triangle)] = MakeMeshBuffer(triangleVerts, std::size(triangleVerts), GL_TRIANGLES);
        m_meshes[PrimitiveToIndex(PrimitiveType::Quad)] = MakeMeshBuffer(quadVerts, std::size(quadVerts), GL_TRIANGLES);
        m_meshes[PrimitiveToIndex(PrimitiveType::Cube)] = MakeMeshBuffer(cubeVerts, std::size(cubeVerts), GL_TRIANGLES);

        if (!m_windows.empty() && m_windows[0].window)
        {
            glfwMakeContextCurrent(m_windows[0].window);
            for (std::size_t i = 0; i < m_meshes.size(); ++i)
                m_windows[0].vaos[i] = CreateVaoForCurrentContext(m_meshes[i]);
        }

        return BuildTestShaderProgram();
    }

    bool OpenGLRenderAdapter::LoadShaderSourcesFromFiles(std::string& outVs, std::string& outFs)
    {
        const std::filesystem::path vsPath = ResolveAssetPath("shaders/test.vert");
        const std::filesystem::path fsPath = ResolveAssetPath("shaders/test.frag");
        if (!std::filesystem::exists(vsPath) || !std::filesystem::exists(fsPath))
            return false;

        std::ifstream vsFile(vsPath, std::ios::in | std::ios::binary);
        std::ifstream fsFile(fsPath, std::ios::in | std::ios::binary);
        if (!vsFile || !fsFile)
            return false;

        std::ostringstream vsStream;
        std::ostringstream fsStream;
        vsStream << vsFile.rdbuf();
        fsStream << fsFile.rdbuf();

        m_vsPath = vsPath;
        m_fsPath = fsPath;
        m_vsLastWrite = std::filesystem::last_write_time(vsPath);
        m_fsLastWrite = std::filesystem::last_write_time(fsPath);
        outVs = vsStream.str();
        outFs = fsStream.str();
        return true;
    }

    bool OpenGLRenderAdapter::BuildTestShaderProgram()
    {
        std::string vsText;
        std::string fsText;
        const bool hasFiles = LoadShaderSourcesFromFiles(vsText, fsText);
        if (!hasFiles)
        {
            Logger::Warn("Shader files not found; using embedded shader sources.");
            vsText = R"GLSL(
                #version 330 core
                layout(location = 0) in vec3 aPos;
                layout(location = 1) in vec2 aUv;
                uniform mat4 uModel;
                uniform mat4 uView;
                uniform mat4 uProjection;
                out vec2 vUv;
                void main()
                {
                    vUv = aUv;
                    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
                }
            )GLSL";

            fsText = R"GLSL(
                #version 330 core
                in vec2 vUv;
                uniform vec4 uColor;
                uniform sampler2D uTexture;
                uniform int uUseTexture;
                out vec4 FragColor;
                void main()
                {
                    vec4 color = uColor;
                    if (uUseTexture != 0)
                        color *= texture(uTexture, vUv);
                    FragColor = color;
                }
            )GLSL";
        }

        const unsigned int vs = CompileShader(GL_VERTEX_SHADER, vsText.c_str());
        if (!vs)
            return false;
        const unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fsText.c_str());
        if (!fs)
        {
            glDeleteShader(vs);
            return false;
        }

        const unsigned int nextProgram = LinkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!nextProgram)
            return false;

        if (m_program)
            glDeleteProgram(m_program);
        m_program = nextProgram;
        m_uModelLoc = glGetUniformLocation(m_program, "uModel");
        m_uViewLoc = glGetUniformLocation(m_program, "uView");
        m_uProjectionLoc = glGetUniformLocation(m_program, "uProjection");
        m_uColorLoc = glGetUniformLocation(m_program, "uColor");
        m_uUseTextureLoc = glGetUniformLocation(m_program, "uUseTexture");
        m_uTextureLoc = glGetUniformLocation(m_program, "uTexture");
        Logger::Info("Shader program built successfully");
        return true;
    }

    void OpenGLRenderAdapter::UpdateShaderHotReload()
    {
        if (m_shaderReloadRequested)
        {
            m_shaderReloadRequested = false;
            Logger::Info("Manual shader reload requested");
            (void)BuildTestShaderProgram();
            return;
        }

        if (m_vsPath.empty() || m_fsPath.empty())
            return;
        if (!std::filesystem::exists(m_vsPath) || !std::filesystem::exists(m_fsPath))
            return;

        const auto vsWrite = std::filesystem::last_write_time(m_vsPath);
        const auto fsWrite = std::filesystem::last_write_time(m_fsPath);
        if (vsWrite != m_vsLastWrite || fsWrite != m_fsLastWrite)
        {
            m_vsLastWrite = vsWrite;
            m_fsLastWrite = fsWrite;
            Logger::Info("Shader file change detected -> rebuilding program");
            (void)BuildTestShaderProgram();
        }
    }

    unsigned int OpenGLRenderAdapter::ResolveTexture(const std::string& texturePath)
    {
        if (texturePath.empty())
            return 0;

        const std::filesystem::path resolvedPath = ResolveAssetPath(texturePath);
        const std::string key = resolvedPath.lexically_normal().generic_string();

        TextureResource& resource = m_textureCache[key];
        if (resource.loadAttempted)
            return resource.glId;

        resource.loadAttempted = true;

        int width = 0;
        int height = 0;
        std::vector<unsigned char> rgbaPixels;
        if (!LoadPpmTextureFile(resolvedPath, width, height, rgbaPixels))
        {
            Logger::Warn("Failed to load texture (PPM expected): ", resolvedPath.string());
            return 0;
        }

        glGenTextures(1, &resource.glId);
        glBindTexture(GL_TEXTURE_2D, resource.glId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);

        Logger::Info("Loaded texture: ", resolvedPath.string());
        return resource.glId;
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
        for (std::size_t i = 0; i < m_meshes.size(); ++i)
            data.vaos[i] = CreateVaoForCurrentContext(m_meshes[i]);

        m_windows.push_back(data);
        Logger::Info("Additional window created. Total windows: ", m_windows.size());
        return true;
    }

    void OpenGLRenderAdapter::RequestShaderReload()
    {
        m_shaderReloadRequested = true;
    }

    void OpenGLRenderAdapter::DestroyTextures()
    {
        for (auto& [_, resource] : m_textureCache)
        {
            if (resource.glId)
            {
                glDeleteTextures(1, &resource.glId);
                resource.glId = 0;
            }
        }
        m_textureCache.clear();
    }

    void OpenGLRenderAdapter::DestroyGLObjects()
    {
        DestroyTextures();

        if (m_program)
        {
            glDeleteProgram(m_program);
            m_program = 0;
        }

        for (auto& mesh : m_meshes)
        {
            if (mesh.vbo)
            {
                glDeleteBuffers(1, &mesh.vbo);
                mesh.vbo = 0;
                mesh.vertexCount = 0;
                mesh.drawMode = 0;
            }
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
}
