#include "OpenGLRenderAdapter.h"

#include "Logger.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <array>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <vector>

namespace archi
{
    static unsigned int CreateTriangleVaoForCurrentContext(unsigned int vbo)
    {
        unsigned int vao = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return vao;
    }

    std::size_t OpenGLRenderAdapter::KeyToIndex(Key key)
    {
        switch (key)
        {
        case Key::Up:
            return 0;
        case Key::Down:
            return 1;
        case Key::Left:
            return 2;
        case Key::Right:
            return 3;
        case Key::Enter:
            return 4;
        case Key::Escape:
            return 5;
        case Key::P:
            return 6;
        case Key::LeftShift:
            return 7;
        case Key::N:
            return 8;
        case Key::R:
            return 9;
        default:
            return static_cast<std::size_t>(-1);
        }
    }

    std::size_t OpenGLRenderAdapter::MouseToIndex(MouseButton button)
    {
        switch (button)
        {
        case MouseButton::Left:
            return 0;
        case MouseButton::Right:
            return 1;
        default:
            return static_cast<std::size_t>(-1);
        }
    }

    OpenGLRenderAdapter::~OpenGLRenderAdapter()
    {
        Shutdown();
    }

    void OpenGLRenderAdapter::OnKeyEvent(int glfwKey, int action)
    {
        auto setDown = [&](Key k) {
            const std::size_t idx = KeyToIndex(k);
            if (idx < m_keyDown.size())
                m_keyDown[idx] = (action != GLFW_RELEASE) ? 1 : 0;
        };

        switch (glfwKey)
        {
        case GLFW_KEY_UP:
            setDown(Key::Up);
            break;
        case GLFW_KEY_DOWN:
            setDown(Key::Down);
            break;
        case GLFW_KEY_LEFT:
            setDown(Key::Left);
            break;
        case GLFW_KEY_RIGHT:
            setDown(Key::Right);
            break;
        case GLFW_KEY_ENTER:
            setDown(Key::Enter);
            break;
        case GLFW_KEY_ESCAPE:
            setDown(Key::Escape);
            break;
        case GLFW_KEY_P:
            setDown(Key::P);
            break;
        case GLFW_KEY_LEFT_SHIFT:
            setDown(Key::LeftShift);
            break;
        case GLFW_KEY_N:
            setDown(Key::N);
            break;
        case GLFW_KEY_R:
            setDown(Key::R);
            break;
        default:
            break;
        }
    }

    void OpenGLRenderAdapter::OnMouseButtonEvent(int glfwButton, int action)
    {
        auto setDown = [&](MouseButton b) {
            const std::size_t idx = MouseToIndex(b);
            if (idx < m_mouseDown.size())
                m_mouseDown[idx] = (action != GLFW_RELEASE) ? 1 : 0;
        };

        switch (glfwButton)
        {
        case GLFW_MOUSE_BUTTON_LEFT:
            setDown(MouseButton::Left);
            break;
        case GLFW_MOUSE_BUTTON_RIGHT:
            setDown(MouseButton::Right);
            break;
        default:
            break;
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

        // A modern baseline.
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

        // Input: prefer stable callback-based state.
        glfwSetWindowUserPointer(primary, this);
        glfwSetKeyCallback(primary, [](GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
            auto* self = static_cast<OpenGLRenderAdapter*>(glfwGetWindowUserPointer(w));
            if (self)
                self->OnKeyEvent(key, action);
        });
        glfwSetMouseButtonCallback(primary, [](GLFWwindow* w, int button, int action, int /*mods*/) {
            auto* self = static_cast<OpenGLRenderAdapter*>(glfwGetWindowUserPointer(w));
            if (self)
                self->OnMouseButtonEvent(button, action);
        });
        glfwSetScrollCallback(primary, [](GLFWwindow* w, double /*xoffset*/, double yoffset) {
            auto* self = static_cast<OpenGLRenderAdapter*>(glfwGetWindowUserPointer(w));
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

        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(primary, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);

        return InitGLObjects();
    }

    void OpenGLRenderAdapter::Shutdown()
    {
        if (m_windows.empty() && m_program == 0 && m_vbo == 0)
            return;

        // Delete per-window VAOs before contexts go away.
        for (auto& w : m_windows)
        {
            if (!w.window)
                continue;
            glfwMakeContextCurrent(w.window);
            if (w.vao)
            {
                glDeleteVertexArrays(1, &w.vao);
                w.vao = 0;
            }
        }

        DestroyGLObjects();

        for (auto& w : m_windows)
        {
            if (w.window)
                glfwDestroyWindow(w.window);
        }
        m_windows.clear();

        glfwTerminate();
    }

    void OpenGLRenderAdapter::BeginFrame()
    {
        UpdateShaderHotReload();

        // Cleanup closed secondary windows.
        for (std::size_t i = 1; i < m_windows.size();)
        {
            if (m_windows[i].window && glfwWindowShouldClose(m_windows[i].window))
            {
                // Delete per-window VAO in its context before destroying.
                glfwMakeContextCurrent(m_windows[i].window);
                if (m_windows[i].vao)
                {
                    glDeleteVertexArrays(1, &m_windows[i].vao);
                    m_windows[i].vao = 0;
                }
                glfwDestroyWindow(m_windows[i].window);
                m_windows.erase(m_windows.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            ++i;
        }

        for (auto& w : m_windows)
        {
            if (!w.window)
                continue;

            glfwMakeContextCurrent(w.window);
            int fbw = 0, fbh = 0;
            glfwGetFramebufferSize(w.window, &fbw, &fbh);
            glViewport(0, 0, fbw, fbh);

            glClearColor(w.clearR, w.clearG, w.clearB, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }

    void OpenGLRenderAdapter::DrawTestPrimitive(const Transform2D& transform)
    {
        const auto mvp = MakeTransformMatrix(transform);

        for (auto& w : m_windows)
        {
            if (!w.window)
                continue;

            glfwMakeContextCurrent(w.window);

            glUseProgram(m_program);
            glUniformMatrix4fv(m_uMvpLoc, 1, GL_FALSE, mvp.data());

            glBindVertexArray(w.vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
        }
    }

    void OpenGLRenderAdapter::EndFrame()
    {
        for (auto& w : m_windows)
        {
            if (!w.window)
                continue;
            glfwMakeContextCurrent(w.window);
            glfwSwapBuffers(w.window);
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

    bool OpenGLRenderAdapter::IsKeyDown(Key key) const
    {
        const std::size_t idx = KeyToIndex(key);
        return idx < m_keyDown.size() ? (m_keyDown[idx] != 0) : false;
    }

    bool OpenGLRenderAdapter::IsMouseButtonDown(MouseButton button) const
    {
        const std::size_t idx = MouseToIndex(button);
        return idx < m_mouseDown.size() ? (m_mouseDown[idx] != 0) : false;
    }

    float OpenGLRenderAdapter::ConsumeScrollDeltaY()
    {
        const float v = m_scrollYAccum;
        m_scrollYAccum = 0.0f;
        return v;
    }

    bool OpenGLRenderAdapter::InitGLObjects()
    {
        // Simple triangle in NDC.
        const float verts[] = {
            -0.5f, -0.4f,
             0.5f, -0.4f,
             0.0f,  0.6f,
        };

        glGenBuffers(1, &m_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // Create a VAO for the primary window context.
        if (!m_windows.empty() && m_windows[0].window)
        {
            glfwMakeContextCurrent(m_windows[0].window);
            m_windows[0].vao = CreateTriangleVaoForCurrentContext(m_vbo);
        }

        // Shader program (file-backed, supports hot reload).
        return BuildTestShaderProgram();
    }

    static bool ReadTextFile(const std::filesystem::path& p, std::string& out)
    {
        std::ifstream f(p, std::ios::in | std::ios::binary);
        if (!f)
            return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }

    bool OpenGLRenderAdapter::LoadShaderSourcesFromFiles(std::string& outVs, std::string& outFs)
    {
        namespace fs = std::filesystem;

        // Try a few candidate locations (works when running from build dir or repo root).
        std::vector<fs::path> roots{
            fs::current_path(),
            fs::current_path() / "..",
            fs::current_path() / ".." / ".."
        };

        for (const auto& root : roots)
        {
            const fs::path vs = root / "assets" / "shaders" / "test.vert";
            const fs::path fsx = root / "assets" / "shaders" / "test.frag";

            if (fs::exists(vs) && fs::exists(fsx))
            {
                std::string vsText, fsText;
                if (ReadTextFile(vs, vsText) && ReadTextFile(fsx, fsText))
                {
                    m_vsPath = vs;
                    m_fsPath = fsx;
                    m_vsLastWrite = fs::last_write_time(vs);
                    m_fsLastWrite = fs::last_write_time(fsx);
                    outVs = std::move(vsText);
                    outFs = std::move(fsText);
                    return true;
                }
            }
        }

        return false;
    }

    bool OpenGLRenderAdapter::BuildTestShaderProgram()
    {
        std::string vsText;
        std::string fsText;

        const bool hasFiles = LoadShaderSourcesFromFiles(vsText, fsText);
        if (!hasFiles)
        {
            Logger::Warn("Shader files not found; using embedded shader sources. (Expected assets/shaders/test.vert + test.frag)");
            vsText = R"GLSL(
                #version 330 core
                layout(location = 0) in vec2 aPos;
                uniform mat4 uMVP;
                void main()
                {
                    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
                }
            )GLSL";

            fsText = R"GLSL(
                #version 330 core
                out vec4 FragColor;
                void main()
                {
                    FragColor = vec4(0.15, 0.85, 0.35, 1.0);
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

        m_uMvpLoc = glGetUniformLocation(m_program, "uMVP");
        if (m_uMvpLoc < 0)
            Logger::Warn("Uniform uMVP not found (shader optimized?)");

        Logger::Info("Shader program built successfully");
        return true;
    }

    void OpenGLRenderAdapter::UpdateShaderHotReload()
    {
        namespace fs = std::filesystem;

        if (m_shaderReloadRequested)
        {
            m_shaderReloadRequested = false;
            Logger::Info("Manual shader reload requested");
            (void)BuildTestShaderProgram();
            return;
        }

        if (m_vsPath.empty() || m_fsPath.empty())
            return;
        if (!fs::exists(m_vsPath) || !fs::exists(m_fsPath))
            return;

        const auto vsWrite = fs::last_write_time(m_vsPath);
        const auto fsWrite = fs::last_write_time(m_fsPath);

        if (vsWrite != m_vsLastWrite || fsWrite != m_fsLastWrite)
        {
            m_vsLastWrite = vsWrite;
            m_fsLastWrite = fsWrite;

            Logger::Info("Shader file change detected -> rebuilding program");
            (void)BuildTestShaderProgram();
        }
    }

    bool OpenGLRenderAdapter::OpenAdditionalWindow(const RenderConfig& cfg, float clearR, float clearG, float clearB)
    {
        if (m_windows.empty() || !m_windows[0].window)
            return false;

        const std::string title = cfg.title + " (Window " + std::to_string(++m_windowCounter) + ")";
        GLFWwindow* sharedWith = m_windows[0].window;

        GLFWwindow* w = glfwCreateWindow(cfg.width, cfg.height, title.c_str(), nullptr, sharedWith);
        if (!w)
        {
            Logger::Error("Failed to create additional window");
            return false;
        }

        glfwMakeContextCurrent(w);
        glfwSwapInterval(cfg.vsync ? 1 : 0);

        glfwSetWindowUserPointer(w, this);
        glfwSetKeyCallback(w, [](GLFWwindow* ww, int key, int /*scancode*/, int action, int /*mods*/) {
            auto* self = static_cast<OpenGLRenderAdapter*>(glfwGetWindowUserPointer(ww));
            if (self)
                self->OnKeyEvent(key, action);
        });
        glfwSetMouseButtonCallback(w, [](GLFWwindow* ww, int button, int action, int /*mods*/) {
            auto* self = static_cast<OpenGLRenderAdapter*>(glfwGetWindowUserPointer(ww));
            if (self)
                self->OnMouseButtonEvent(button, action);
        });
        glfwSetScrollCallback(w, [](GLFWwindow* ww, double /*xoffset*/, double yoffset) {
            auto* self = static_cast<OpenGLRenderAdapter*>(glfwGetWindowUserPointer(ww));
            if (self)
                self->OnScrollEvent(yoffset);
        });

        glfwSetInputMode(w, GLFW_STICKY_KEYS, GLFW_TRUE);
        glfwSetInputMode(w, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);

        WindowData data{};
        data.window = w;
        data.clearR = clearR;
        data.clearG = clearG;
        data.clearB = clearB;
        data.vao = CreateTriangleVaoForCurrentContext(m_vbo);

        m_windows.push_back(data);
        Logger::Info("Additional window created. Total windows: ", m_windows.size());
        return true;
    }

    void OpenGLRenderAdapter::RequestShaderReload()
    {
        m_shaderReloadRequested = true;
    }

    void OpenGLRenderAdapter::DestroyGLObjects()
    {
        if (m_program)
        {
            glDeleteProgram(m_program);
            m_program = 0;
        }
        if (m_vbo)
        {
            glDeleteBuffers(1, &m_vbo);
            m_vbo = 0;
        }
    }

    unsigned int OpenGLRenderAdapter::CompileShader(unsigned int type, const char* src)
    {
        const unsigned int id = glCreateShader(type);
        glShaderSource(id, 1, &src, nullptr);
        glCompileShader(id);

        int ok = 0;
        glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            int len = 0;
            glGetShaderiv(id, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> buf(static_cast<size_t>(len) + 1);
            glGetShaderInfoLog(id, len, nullptr, buf.data());

            Logger::Error("Shader compile failed: ", buf.data());
            glDeleteShader(id);
            return 0;
        }
        return id;
    }

    unsigned int OpenGLRenderAdapter::LinkProgram(unsigned int vs, unsigned int fs)
    {
        const unsigned int prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);

        int ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            int len = 0;
            glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> buf(static_cast<size_t>(len) + 1);
            glGetProgramInfoLog(prog, len, nullptr, buf.data());

            Logger::Error("Program link failed: ", buf.data());
            glDeleteProgram(prog);
            return 0;
        }

        return prog;
    }
}

