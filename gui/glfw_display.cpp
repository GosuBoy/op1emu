#include "glfw_display.h"
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

GLFWDisplay::GLFWDisplay(const std::string &configPath, bool floating) : floating_(floating) {
    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW");
    try {
        glfwDefaultWindowHints();
        if (floating_) {
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
            glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
            glfwWindowHint(GLFW_ALPHA_BITS, 8);
        }
        window_ = glfwCreateWindow(640, 480, "OP1 Emulator", nullptr, nullptr);
        if (!window_ && floating_) {
            std::cerr << "Floating window creation failed; using a standard window\n";
            floating_ = false;
            glfwDefaultWindowHints();
            window_ = glfwCreateWindow(640, 480, "OP1 Emulator", nullptr, nullptr);
        }
        if (!window_)
            throw std::runtime_error("Failed to create GLFW window");
        glfwMakeContextCurrent(window_);
        glfwSwapInterval(1);
        glfwSetWindowUserPointer(window_, this);
        glfwSetMouseButtonCallback(window_, MouseButtonCallback);
        glfwSetCursorPosCallback(window_, CursorPositionCallback);
        glfwSetKeyCallback(window_, KeyCallback);
        glfwSetWindowFocusCallback(window_, FocusCallback);
        input_.event = [this](int bank, int pin, bool pressed) {
            if (keyEventCallback)
                keyEventCallback(bank, pin, pressed);
        };
        try {
            LoadPanel(configPath);
        } catch (const std::exception &e) {
            if (configPath == "gui/ui.json")
                throw;
            std::cerr << "UI config failed: " << e.what() << "; falling back to gui/ui.json\n";
            LoadPanel("gui/ui.json");
        }
        glfwSetWindowSize(window_, std::max(1, int(artWidth_ * uiConfig_.scale)),
                          std::max(1, int(artHeight_ * uiConfig_.scale)));
        if (floating_) {
            if (!svg_ || !glfwGetWindowAttrib(window_, GLFW_TRANSPARENT_FRAMEBUFFER)) {
                std::cerr << "Floating UI needs SVG artwork and desktop transparency; using a standard window\n";
                RestoreWindowFrame();
            } else {
                glfwSetWindowAspectRatio(window_, std::max(1, int(std::lround(artWidth_))),
                                         std::max(1, int(std::lround(artHeight_))));
            }
        }
    } catch (...) {
        Cleanup();
        throw;
    }
}
void GLFWDisplay::Cleanup() {
    if (window_) {
        glfwMakeContextCurrent(window_);
        if (texture_)
            glDeleteTextures(1, &texture_);
        if (backgroundTexture_)
            glDeleteTextures(1, &backgroundTexture_);
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}
GLFWDisplay::~GLFWDisplay() { Cleanup(); }
void GLFWDisplay::UploadBackground(const uint8_t *data, int width, int height) {
    GLint limit;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (width > limit || height > limit)
        throw std::runtime_error("Background exceeds GL texture size");
    glPushAttrib(GL_TEXTURE_BIT);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
    if (!backgroundTexture_)
        glGenTextures(1, &backgroundTexture_);
    glBindTexture(GL_TEXTURE_2D, backgroundTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glPopClientAttrib();
    glPopAttrib();
}
void GLFWDisplay::LoadPanel(const std::string &path) {
    auto config = UILayout::Load(path);
    std::unique_ptr<SvgBackgroundRenderer> svg;
    if (config.svg) {
        svg = std::make_unique<SvgBackgroundRenderer>(config.background);
        auto pixels = svg->Render(uint32_t(std::ceil(svg->Width())), uint32_t(std::ceil(svg->Height())));
        UploadBackground(pixels.rgba.data(), pixels.width, pixels.height);
        artWidth_ = svg->Width();
        artHeight_ = svg->Height();
        cachedWidth_ = pixels.width;
        cachedHeight_ = pixels.height;
    } else {
        int width, height, channels;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> data(
            stbi_load(config.background.c_str(), &width, &height, &channels, 4), stbi_image_free);
        if (!data)
            throw std::runtime_error("Cannot load PNG: " + config.background);
        UploadBackground(data.get(), width, height);
        artWidth_ = width;
        artHeight_ = height;
    }
    uiConfig_ = std::move(config);
    svg_ = std::move(svg);
}
void GLFWDisplay::Initialize(int rows, int lines) {
    if (rows <= 0 || lines <= 0 || rows > 8192 || lines > 8192)
        throw std::runtime_error("Invalid framebuffer size");
    std::lock_guard<std::mutex> lock(framebufferMutex_);
    if (rows == width_ && lines == height_)
        return;
    width_ = rows;
    height_ = lines;
    framebuffer_.assign(size_t(rows) * lines, 0);
    dirty_ = true;
}
void GLFWDisplay::UpdateRowBuffer(int x, int y, const void *data, int length) {
    std::lock_guard<std::mutex> lock(framebufferMutex_);
    if (!data || length <= 0 || y < 0 || y >= height_ || x < 0 || x >= width_)
        return;
    const int pixels = std::min(length / int(sizeof(uint16_t)), width_ - x);
    if (pixels > 0) {
        std::memcpy(framebuffer_.data() + size_t(y) * width_ + x, data, size_t(pixels) * 2);
        dirty_ = true;
    }
}
void GLFWDisplay::UpdateSvgCache(int lw, int lh, int fw, int fh) {
    if (!svg_)
        return;
    const auto fit = UIFit::Into(artWidth_, artHeight_, lw, lh);
    double w = artWidth_ * fit.scale * fw / lw, h = artHeight_ * fit.scale * fh / lh;
    GLint maxTexture;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    const double limit = std::min(8192, maxTexture);
    double reduction = std::min({1.0, limit / w, limit / h, std::sqrt((16.0 * 1024 * 1024) / (w * h))});
    const int targetWidth = std::max(1, int(std::floor(w * reduction))),
              targetHeight = std::max(1, int(std::floor(h * reduction)));
    if (cachedWidth_ == targetWidth && cachedHeight_ == targetHeight)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (targetWidth != pendingWidth_ || targetHeight != pendingHeight_) {
        pendingWidth_ = targetWidth;
        pendingHeight_ = targetHeight;
        resizeChanged_ = now;
        return;
    }
    if (now - resizeChanged_ < std::chrono::milliseconds(120))
        return;
    try {
        auto pixels = svg_->Render(targetWidth, targetHeight);
        UploadBackground(pixels.rgba.data(), targetWidth, targetHeight);
        cachedWidth_ = targetWidth;
        cachedHeight_ = targetHeight;
    } catch (const std::exception &e) {
        std::cerr << "SVG resize failed: " << e.what() << "; keeping cached texture\n";
        // Avoid retrying expensive filters every frame after a failed allocation.
        cachedWidth_ = targetWidth;
        cachedHeight_ = targetHeight;
    }
}
void GLFWDisplay::DrawBackground() {
    glBlendFunc(svg_ ? GL_ONE : GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, backgroundTexture_);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2d(0, 0);
    glTexCoord2f(1, 0);
    glVertex2d(artWidth_, 0);
    glTexCoord2f(1, 1);
    glVertex2d(artWidth_, artHeight_);
    glTexCoord2f(0, 1);
    glVertex2d(0, artHeight_);
    glEnd();
}
void GLFWDisplay::DrawScreen() {
    // Map the portrait DMA texture to TL/TR/BR/BL in landscape. Homogeneous
    // vertices give perspective-correct interpolation for non-affine quads.
    const auto matrix = UIScreenMatrix(uiConfig_.screen);
    glPushMatrix();
    glMultMatrixd(matrix.data());
    glDisable(GL_BLEND); // firmware RGB565 is opaque, independent of SVG alpha
    if (texture_) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture_);
        const GLint filter = uiConfig_.screenFilter == UIScreenFilter::Linear ? GL_LINEAR : GL_NEAREST;
        // Apply at draw time: a panel switch must work even without a new
        // firmware frame. PNG/custom layouts keep nearest unless opted in.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    } else {
        glDisable(GL_TEXTURE_2D);
        glColor4f(0, 0, 0, 1);
    }
    constexpr double pi = 3.14159265358979323846;
    const double c = std::cos(rotation_ * pi / 180), s = std::sin(rotation_ * pi / 180);
    const std::array<UIPoint, 4> corners = {UIPoint{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    glBegin(GL_QUADS);
    for (auto p : corners) {
        const double x = p.x - .5, y = p.y - .5;
        glTexCoord2d(c * x + s * y + .5, -s * x + c * y + .5);
        glVertex2d(p.x, p.y);
    }
    glEnd();
    glColor4f(1, 1, 1, 1);
    glPopMatrix();
}
void GLFWDisplay::RenderFramebuffer(bool present) {
    if (!window_)
        return;
    glfwMakeContextCurrent(window_);
    int fw, fh, lw, lh;
    glfwGetFramebufferSize(window_, &fw, &fh);
    glfwGetWindowSize(window_, &lw, &lh);
    if (fw <= 0 || fh <= 0 || lw <= 0 || lh <= 0)
        return; // minimized
    UpdateSvgCache(lw, lh, fw, fh);
    // The compatibility context is owned by this frontend. Restore its GL
    // state so premultiplied SVG state never leaks into PNG or display upload.
    GLint matrixMode;
    glGetIntegerv(GL_MATRIX_MODE, &matrixMode);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE); // RGB565 rows may be an odd width
    {
        std::lock_guard<std::mutex> lock(framebufferMutex_);
        if (dirty_) {
            if (!texture_)
                glGenTextures(1, &texture_);
            glBindTexture(GL_TEXTURE_2D, texture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            if (textureWidth_ != width_ || textureHeight_ != height_) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width_, height_, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
                             framebuffer_.data());
                textureWidth_ = width_;
                textureHeight_ = height_;
            } else
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
                                framebuffer_.data());
            dirty_ = false;
        }
    }
    glViewport(0, 0, fw, fh);
    if (floating_)
        glClearColor(0, 0, 0, 0);
    else
        glClearColor(.12f, .12f, .12f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, lw, lh, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    const auto fit = UIFit::Into(artWidth_, artHeight_, lw, lh);
    glTranslated(fit.x, fit.y, 0);
    glScaled(fit.scale, fit.scale, 1);
    glColor4f(1, 1, 1, 1);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    DrawBackground();
    DrawScreen();
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(matrixMode);
    glPopClientAttrib();
    glPopAttrib();
    if (present)
        glfwSwapBuffers(window_);
}
void GLFWDisplay::PollEvents() {
    glfwPollEvents();
    RenderFramebuffer();
}
bool GLFWDisplay::ShouldClose() const { return window_ && glfwWindowShouldClose(window_); }
void GLFWDisplay::SwapBuffers() {
    if (window_)
        glfwSwapBuffers(window_);
}
void GLFWDisplay::SetDisplayRotation(int degrees) { rotation_ = (degrees % 360 + 360) % 360; }
bool GLFWDisplay::IsControl(UIPoint point) const {
    for (const auto &b : uiConfig_.buttons)
        if (b.region.Contains(point))
            return true;
    for (const auto &e : uiConfig_.encoders)
        if (e.clockwise.Contains(point) || e.counterClockwise.Contains(point))
            return true;
    return uiConfig_.volumeUp.Contains(point) || uiConfig_.volumeDown.Contains(point);
}
void GLFWDisplay::RestoreWindowFrame() {
    floating_ = dragging_ = false;
    glfwSetWindowAttrib(window_, GLFW_DECORATED, GLFW_TRUE);
    glfwSetWindowAspectRatio(window_, GLFW_DONT_CARE, GLFW_DONT_CARE);
}
void GLFWDisplay::ResizeFloating(double factor) {
    int width, height;
    glfwGetWindowSize(window_, &width, &height);
    const int base = std::max(4, int(std::lround(artWidth_ * uiConfig_.scale)));
    width = std::clamp(int(std::lround(width * factor)), base / 4, base * 3);
    height = std::max(1, int(std::lround(width * artHeight_ / artWidth_)));
    glfwSetWindowSize(window_, width, height);
}
void GLFWDisplay::CursorPositionCallback(GLFWwindow *w, double x, double y) {
    auto *self = static_cast<GLFWDisplay *>(glfwGetWindowUserPointer(w));
    self->cursorPosition_ = {x, y};
    self->hasCursorPosition_ = true;
    if (self->dragging_) {
        int wx, wy;
        glfwGetWindowPos(w, &wx, &wy);
        glfwSetWindowPos(w, wx + int(std::lround(x - self->dragOffset_.x)),
                         wy + int(std::lround(y - self->dragOffset_.y)));
    }
}
void GLFWDisplay::MouseButtonCallback(GLFWwindow *w, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || (action != GLFW_PRESS && action != GLFW_RELEASE))
        return;
    auto *self = static_cast<GLFWDisplay *>(glfwGetWindowUserPointer(w));
    double x, y;
    int width, height;
    // Native/remote event coordinates can differ from a global cursor query
    // (notably macOS window sharing). Prefer the latest GLFW cursor event.
    if (self->hasCursorPosition_) {
        x = self->cursorPosition_.x;
        y = self->cursorPosition_.y;
    } else {
        glfwGetCursorPos(w, &x, &y);
    }
    glfwGetWindowSize(w, &width, &height);
    if (action == GLFW_RELEASE) {
        self->dragging_ = false;
        self->input_.Mouse(false, {});
    } else if (width > 0 && height > 0) {
        const auto point = UIFit::Into(self->artWidth_, self->artHeight_, width, height).ToArt({x, y});
        if (self->floating_ && point.x >= 0 && point.y >= 0 && point.x <= self->artWidth_ &&
            point.y <= self->artHeight_ && !self->IsControl(point)) {
            self->dragging_ = true;
            self->dragOffset_ = {x, y};
        } else
            self->input_.Mouse(true, point);
    }
}
void GLFWDisplay::KeyCallback(GLFWwindow *w, int key, int, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_RELEASE)
        return;
    auto *self = static_cast<GLFWDisplay *>(glfwGetWindowUserPointer(w));
    if (self->floating_ && action == GLFW_PRESS) {
        if (key == GLFW_KEY_ESCAPE) {
            self->RestoreWindowFrame();
            return;
        }
        if (mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) {
            if (key == GLFW_KEY_W) {
                glfwSetWindowShouldClose(w, GLFW_TRUE);
                return;
            }
            if (key == GLFW_KEY_M) {
                glfwIconifyWindow(w);
                return;
            }
            if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) {
                self->ResizeFloating(1.1);
                return;
            }
            if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
                self->ResizeFloating(1 / 1.1);
                return;
            }
        }
    }
    for (auto &k : self->uiConfig_.keys)
        if (key == self->MapKeyNameToGLFW(k.name)) {
            self->input_.Key(key, k.gpio, action == GLFW_PRESS);
            break;
        }
}
void GLFWDisplay::FocusCallback(GLFWwindow *w, int focused) {
    if (!focused) {
        auto *self = static_cast<GLFWDisplay *>(glfwGetWindowUserPointer(w));
        self->input_.ReleaseAll();
        self->hasCursorPosition_ = false;
        self->dragging_ = false;
    }
}
int GLFWDisplay::MapKeyNameToGLFW(const std::string &keyName) {
    // Map common key names to GLFW key codes
    if (keyName == "shift")
        return GLFW_KEY_LEFT_SHIFT;
    if (keyName == "ctrl" || keyName == "control")
        return GLFW_KEY_LEFT_CONTROL;
    if (keyName == "alt")
        return GLFW_KEY_LEFT_ALT;
    if (keyName == "space")
        return GLFW_KEY_SPACE;
    if (keyName == "enter" || keyName == "return")
        return GLFW_KEY_ENTER;
    if (keyName == "tab")
        return GLFW_KEY_TAB;
    if (keyName == "escape" || keyName == "esc")
        return GLFW_KEY_ESCAPE;
    if (keyName == "backspace")
        return GLFW_KEY_BACKSPACE;

    // Arrow keys
    if (keyName == "up")
        return GLFW_KEY_UP;
    if (keyName == "down")
        return GLFW_KEY_DOWN;
    if (keyName == "left")
        return GLFW_KEY_LEFT;
    if (keyName == "right")
        return GLFW_KEY_RIGHT;

    // Single character keys (a-z, 0-9)
    if (keyName.length() == 1) {
        char c = keyName[0];
        if (c >= 'a' && c <= 'z')
            return GLFW_KEY_A + (c - 'a');
        if (c >= 'A' && c <= 'Z')
            return GLFW_KEY_A + (c - 'A');
        if (c >= '0' && c <= '9')
            return GLFW_KEY_0 + (c - '0');
    }

    // Default: return the key name as-is if it's already a valid GLFW key code
    return GLFW_KEY_UNKNOWN;
}
