#include "glfw_display.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <tuple>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// This harness never constructs a CPU or opens NAND/OTP/audio. The diagnostic
// framebuffer tests the very same RGB565 upload/rotation path firmware uses.
class UIPreview {
  public:
    static void Require(bool ok, const char *message) {
        if (!ok)
            throw std::runtime_error(message);
    }
    static void Feed(GLFWDisplay &d, int width, int height) {
        d.Initialize(width, height);
        for (int y = 0; y < height; ++y) {
            std::vector<uint16_t> row(width);
            for (int x = 0; x < width; ++x)
                row[x] =
                    y < height / 2 ? (x < width / 2 ? 0xf800 : 0x07e0) : (x < width / 2 ? 0x001f : 0xffff);
            d.UpdateRowBuffer(0, y, row.data(), width * 2);
        }
    }
    static std::vector<uint8_t> Read(GLFWDisplay &d, int &w, int &h) {
        glfwGetFramebufferSize(d.window_, &w, &h);
        std::vector<uint8_t> rgba(size_t(w) * h * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        // Read the rendered back buffer before presentation so capture does
        // not depend on virtual-display/front-buffer behavior after a swap.
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        Require(glGetError() == GL_NO_ERROR, "OpenGL error during capture");
        for (int y = 0; y < h / 2; ++y)
            for (int x = 0; x < w * 4; ++x)
                std::swap(rgba[size_t(y) * w * 4 + x], rgba[size_t(h - 1 - y) * w * 4 + x]);
        return rgba;
    }
    static void Check(GLFWDisplay &d) {
        int lw, lh, w, h;
        glfwGetWindowSize(d.window_, &lw, &lh);
        auto pixels = Read(d, w, h);
        auto fit = UIFit::Into(d.artWidth_, d.artHeight_, lw, lh);
        auto matrix = UIScreenMatrix(d.uiConfig_.screen);
        int index = 0;
        // Clockwise rotation maps source BL/TL/BR/TR to screen TL/TR/BL/BR.
        const int expected[4][3] = {{0, 0, 255}, {255, 0, 0}, {255, 255, 255}, {0, 255, 0}};
        for (auto p : {UIPoint{.25, .25}, {.75, .25}, {.25, .75}, {.75, .75}}) {
            double q = matrix[3] * p.x + matrix[7] * p.y + 1;
            int x = int((fit.x + fit.scale * (matrix[0] * p.x + matrix[4] * p.y + matrix[12]) / q) * w / lw);
            int y = int((fit.y + fit.scale * (matrix[1] * p.x + matrix[5] * p.y + matrix[13]) / q) * h / lh);
            auto pixel = pixels.data() + (size_t(y) * w + x) * 4;
            if (std::abs(int(pixel[0]) - expected[index][0]) >= 3 ||
                std::abs(int(pixel[1]) - expected[index][1]) >= 3 ||
                std::abs(int(pixel[2]) - expected[index][2]) >= 3)
                std::cerr << "screen sample " << index << " at " << x << ',' << y << " (" << w << 'x' << h
                          << ") = " << int(pixel[0]) << ',' << int(pixel[1]) << ',' << int(pixel[2]) << '\n';
            for (int c = 0; c < 3; ++c)
                Require(std::abs(int(pixels[(size_t(y) * w + x) * 4 + c]) - expected[index][c]) < 3,
                        "RGB565 screen rotation/corner mismatch");
            ++index;
        }
    }
    static std::array<uint8_t, 3> ScreenCenter(GLFWDisplay &d) {
        int lw, lh, w, h;
        glfwGetWindowSize(d.window_, &lw, &lh);
        auto pixels = Read(d, w, h);
        auto fit = UIFit::Into(d.artWidth_, d.artHeight_, lw, lh);
        auto m = UIScreenMatrix(d.uiConfig_.screen);
        const double q = (m[3] + m[7]) * .5 + 1;
        const int x = int((fit.x + fit.scale * ((m[0] + m[4]) * .5 + m[12]) / q) * w / lw);
        const int y = int((fit.y + fit.scale * ((m[1] + m[5]) * .5 + m[13]) / q) * h / lh);
        Require(x >= 0 && x < w && y >= 0 && y < h, "screen center lies inside drawable");
        auto p = pixels.data() + (size_t(y) * w + x) * 4;
        return {p[0], p[1], p[2]};
    }
    static int Run(int argc, char **argv) {
        bool test = argc == 2 && std::string(argv[1]) == "--test";
        std::string path = test ? (SvgBackgroundRenderer::Available() ? "gui/ui-svg.json" : "gui/ui.json")
                                : (argc > 1 ? argv[1] : "gui/ui.json");
        GLFWDisplay d(path);
        Feed(d, 201, 400); // odd-width upload exercises UNPACK_ALIGNMENT=1
        glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 31);
        glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        const auto draw = [&] {
            glfwPollEvents();
            // Acquire a drawable at the new window size before rendering. In
            // particular GLX may defer reallocating swap buffers until a swap.
            glfwSwapBuffers(d.window_);
            d.RenderFramebuffer(false);
        };
        draw();
        std::this_thread::sleep_for(std::chrono::milliseconds(140));
        draw();
        if (test) {
            GLint alignment, src, dst, rowLength, swapBytes;
            glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
            glGetIntegerv(GL_UNPACK_SWAP_BYTES, &swapBytes);
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
            glGetIntegerv(GL_BLEND_SRC, &src);
            glGetIntegerv(GL_BLEND_DST, &dst);
            Require(alignment == 8 && rowLength == 31 && swapBytes == GL_TRUE && src == GL_SRC_ALPHA &&
                        dst == GL_ONE,
                    "GL blend/unpack state restoration");
            Check(d);
            // A magnified 2x2 RGB565 frame has a gray center with bilinear
            // sampling and a pure source color with nearest. Switch without
            // another frame upload to exercise draw-time filter selection.
            const auto defaultFilter = d.uiConfig_.screenFilter;
            Feed(d, 2, 2);
            d.uiConfig_.screenFilter = UIScreenFilter::Linear;
            draw();
            for (auto c : ScreenCenter(d))
                Require(std::abs(int(c) - 128) <= 12, "linear sampling interpolates screen pixels");
            const auto screenTexture = d.texture_;
            d.uiConfig_.screenFilter = UIScreenFilter::Nearest;
            draw();
            for (auto c : ScreenCenter(d))
                Require(c == 0 || c == 255, "nearest sampling preserves source colors");
            Require(d.texture_ == screenTexture, "filter change reuses display texture");
            d.uiConfig_.screenFilter = defaultFilter;
            Feed(d, 201, 400);
            draw();
            const auto texture = d.backgroundTexture_;
            glfwSetWindowSize(d.window_, 802, 500);
            glfwPollEvents();
            draw();
            std::this_thread::sleep_for(std::chrono::milliseconds(140));
            draw();
            Check(d);
            Require(d.backgroundTexture_ == texture, "resize reuses GL background texture");
            Feed(d, 203, 402);
            draw();
            Check(d);
            // An arbitrary projective screen quad must also keep corner colors.
            d.uiConfig_.screen = {UIPoint{284, 30}, {530, 40}, {515, 145}, {300, 135}};
            draw();
            Check(d);
            Require(glGetError() == GL_NO_ERROR, "GL resources and geometry changes");
            if (SvgBackgroundRenderer::Available()) {
                d.LoadPanel("tests/fixtures/svg/ui-alpha.json");
                draw();
                int w, h, lw, lh;
                auto rgba = Read(d, w, h);
                glfwGetWindowSize(d.window_, &lw, &lh);
                auto fit = UIFit::Into(20, 10, lw, lh);
                int x = int((fit.x + 15 * fit.scale) * w / lw), y = int((fit.y + 5 * fit.scale) * h / lh);
                auto pixel = rgba.data() + (size_t(y) * w + x) * 4;
                Require(std::abs(int(pixel[0]) - 15) <= 2 && std::abs(int(pixel[1]) - 143) <= 2 &&
                            std::abs(int(pixel[2]) - 15) <= 2,
                        "premultiplied alpha composites without dark/bright halos");
            }
            d.LoadPanel("gui/ui.json");
            Feed(d, 201, 400);
            draw();
            Check(d);
            Require(!d.svg_, "PNG remains usable after SVG");
            // Native window-sharing tools deliver correct cursor events while
            // a separate global glfwGetCursorPos query may remain stale.
            auto synth = std::find_if(d.uiConfig_.buttons.begin(), d.uiConfig_.buttons.end(),
                                      [](const auto &b) { return b.name == "synth"; });
            Require(synth != d.uiConfig_.buttons.end(), "Synth control exists");
            int inputWidth, inputHeight;
            glfwGetWindowSize(d.window_, &inputWidth, &inputHeight);
            auto inputFit = UIFit::Into(d.artWidth_, d.artHeight_, inputWidth, inputHeight);
            UIPoint center{};
            for (auto p : synth->region.polygon) {
                center.x += p.x / 4;
                center.y += p.y / 4;
            }
            std::vector<std::tuple<int, int, bool>> keys;
            d.SetKeyEventCallback([&](int bank, int pin, bool down) { keys.emplace_back(bank, pin, down); });
            GLFWDisplay::CursorPositionCallback(d.window_, inputFit.x + center.x * inputFit.scale,
                                                inputFit.y + center.y * inputFit.scale);
            GLFWDisplay::MouseButtonCallback(d.window_, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
            GLFWDisplay::CursorPositionCallback(d.window_, -100, -100);
            GLFWDisplay::MouseButtonCallback(d.window_, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
            Require(keys == std::vector<std::tuple<int, int, bool>>{{3, 2, true}, {3, 2, false}},
                    "mouse callbacks use event coordinates and release outside");
            GLFWDisplay::FocusCallback(d.window_, GLFW_FALSE);
            Require(!d.hasCursorPosition_, "focus loss invalidates stale cursor position");
            d.SetKeyEventCallback({});
            std::cout
                << "GL RGB565, filtering, orientation, projective overlay, state, resize and texture reuse passed\n";
        } else if (argc > 2) {
            int w, h;
            auto pixels = Read(d, w, h);
            Require(stbi_write_png(argv[2], w, h, 4, pixels.data(), w * 4), "capture write failed");
            std::cout << w << 'x' << h << " capture written\n";
        } else
            while (!d.ShouldClose()) {
                draw();
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        return 0;
    }
};
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--probe-context") {
            // Capability probe only: renderer/assertion failures never skip.
            glfwSetErrorCallback(
                [](int code, const char *detail) { std::cerr << "GLFW " << code << ": " << detail << '\n'; });
            if (!glfwInit())
                return 77;
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            auto *window = glfwCreateWindow(640, 480, "OpenGL capability probe", nullptr, nullptr);
            if (!window) {
                glfwTerminate();
                return 77;
            }
            glfwDestroyWindow(window);
            glfwTerminate();
            return 0;
        }
        return UIPreview::Run(argc, argv);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
