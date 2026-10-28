#pragma once
#include "peripheral/display.h"
#include "peripheral/keyboard.h"
#include "svg_background_renderer.h"
#include "ui_layout.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

class GLFWDisplay : public Display, public Keyboard {
  public:
    explicit GLFWDisplay(const std::string &configPath = "gui/ui.json");
    ~GLFWDisplay() override;
    void Initialize(int rows, int lines) override;
    void UpdateRowBuffer(int x, int y, const void *data, int length) override;
    void RenderFramebuffer(bool present = true);
    void PollEvents();
    bool ShouldClose() const;
    void SwapBuffers();
    void SetDisplayRotation(int degrees);
    uint8_t GetVolumeValue() const { return input_.Volume(); }

  private:
    friend class UIPreview; // firmware-free GL regression/capture harness
    void LoadPanel(const std::string &path);
    void UploadBackground(const uint8_t *data, int width, int height);
    void UpdateSvgCache(int logicalWidth, int logicalHeight, int fbWidth, int fbHeight);
    void Cleanup();
    static void MouseButtonCallback(GLFWwindow *, int button, int action, int mods);
    static void KeyCallback(GLFWwindow *, int key, int scancode, int action, int mods);
    static void FocusCallback(GLFWwindow *, int focused);
    int MapKeyNameToGLFW(const std::string &name);
    void DrawBackground();
    void DrawScreen();
    GLFWwindow *window_ = nullptr;
    GLuint texture_ = 0, backgroundTexture_ = 0;
    std::vector<uint16_t> framebuffer_;
    int width_ = 0, height_ = 0, textureWidth_ = 0, textureHeight_ = 0;
    int rotation_ = 90;
    bool dirty_ = false;
    std::mutex framebufferMutex_;
    UILayout uiConfig_;
    UIPanelInput input_{uiConfig_};
    std::unique_ptr<SvgBackgroundRenderer> svg_;
    double artWidth_ = 0, artHeight_ = 0;
    int cachedWidth_ = 0, cachedHeight_ = 0, pendingWidth_ = 0, pendingHeight_ = 0;
    std::chrono::steady_clock::time_point resizeChanged_{};
};
