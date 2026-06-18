#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <vector>

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "mandelbrot_fractal_utils.hpp"
#include "mandelbrot_sender.hpp"
#include "types_sfml.hpp"

namespace ex = stdexec;
using namespace mandelbrot;

TEST(MandelbrotMath, PointInsideSetReachesMaxIterations) {
    constexpr std::uint32_t max_iterations = 100;
    EXPECT_EQ(CalculateIterationsForPoint(Complex{0.0, 0.0}, max_iterations, 2.0), max_iterations);
    EXPECT_EQ(CalculateIterationsForPoint(Complex{-0.5, 0.0}, max_iterations, 2.0), max_iterations);
}

TEST(MandelbrotMath, PointOutsideSetEscapesEarly) {
    constexpr std::uint32_t max_iterations = 100;
    const std::uint32_t iters = CalculateIterationsForPoint(Complex{2.0, 2.0}, max_iterations, 2.0);
    EXPECT_LT(iters, max_iterations);
    EXPECT_GT(max_iterations, 0u);
}

TEST(MandelbrotMath, PixelToComplexMapsCorners) {
    const ViewPort vp{-2.0, 1.0, -1.5, 1.5};
    constexpr std::uint32_t w = 300;
    constexpr std::uint32_t h = 200;

    const Complex top_left = Pixel2DToComplex(0, 0, vp, w, h);
    EXPECT_DOUBLE_EQ(top_left.real(), vp.x_min);
    EXPECT_DOUBLE_EQ(top_left.imag(), vp.y_min);

    const Complex bottom_right = Pixel2DToComplex(w, h, vp, w, h);
    EXPECT_DOUBLE_EQ(bottom_right.real(), vp.x_max);
    EXPECT_DOUBLE_EQ(bottom_right.imag(), vp.y_max);
}

TEST(MandelbrotMath, ColorOfSetMemberIsBlack) {
    constexpr std::uint32_t max_iterations = 100;
    const RgbColor color = IterationsToColor(max_iterations, max_iterations);
    EXPECT_EQ(color.r, 0);
    EXPECT_EQ(color.g, 0);
    EXPECT_EQ(color.b, 0);
}

TEST(MandelbrotMath, ColorOfEscapedPointIsNotBlack) {
    constexpr std::uint32_t max_iterations = 100;
    const RgbColor color = IterationsToColor(50, max_iterations);
    EXPECT_TRUE(color.r != 0 || color.g != 0 || color.b != 0);
}

namespace {

struct CapturingReceiver {
    using receiver_concept = ex::receiver_tag;

    FrameBuffer **out;
    bool *value_called;
    bool *error_called;

    void set_value(FrameBuffer *fb) && noexcept {
        *out = fb;
        *value_called = true;
    }

    void set_error(std::exception_ptr) && noexcept { *error_called = true; }

    void set_stopped() && noexcept {}
};

std::vector<sf::Uint8> RenderReference(const RenderSettings &s, const ViewPort &vp) {
    std::vector<sf::Uint8> rgba(static_cast<std::size_t>(s.width) * s.height * 4u);
    for (std::uint32_t row = 0; row < s.height; ++row) {
        for (std::uint32_t col = 0; col < s.width; ++col) {
            const Complex point = Pixel2DToComplex(col, row, vp, s.width, s.height);
            const std::uint32_t iterations = CalculateIterationsForPoint(point, s.max_iterations, s.escape_radius);
            const RgbColor color = IterationsToColor(iterations, s.max_iterations);
            const std::size_t idx = (static_cast<std::size_t>(row) * s.width + col) * 4u;
            rgba[idx + 0] = color.r;
            rgba[idx + 1] = color.g;
            rgba[idx + 2] = color.b;
            rgba[idx + 3] = 255;
        }
    }
    return rgba;
}

}  // namespace

TEST(ComputeSender, CustomReceiverGetsFilledFrameBuffer) {
    const RenderSettings settings{.width = 16, .height = 12, .max_iterations = 50, .escape_radius = 2.0};
    const ViewPort viewport = AppState::INITIAL_VIEWPORT;

    FrameBuffer fb = FrameBuffer::Make(settings.width, settings.height);

    FrameBuffer *result = nullptr;
    bool value_called = false;
    bool error_called = false;

    auto sender = ex::just(&fb) | MakeComputeSender(settings, viewport);
    auto op = ex::connect(std::move(sender), CapturingReceiver{&result, &value_called, &error_called});
    ex::start(op);

    EXPECT_TRUE(value_called);
    EXPECT_FALSE(error_called);
    EXPECT_EQ(result, &fb);

    for (std::size_t i = 3; i < fb.rgba.size(); i += 4) {
        EXPECT_EQ(fb.rgba[i], 255);
    }
}

TEST(ComputeSenderIntegration, MatchesReferenceWhenRunInline) {
    const RenderSettings settings{.width = 32, .height = 24, .max_iterations = 80, .escape_radius = 2.0};
    const ViewPort viewport = AppState::INITIAL_VIEWPORT;

    FrameBuffer fb = FrameBuffer::Make(settings.width, settings.height);
    auto [out] = ex::sync_wait(ex::just(&fb) | MakeComputeSender(settings, viewport)).value();

    EXPECT_EQ(out, &fb);
    EXPECT_EQ(fb.rgba, RenderReference(settings, viewport));
}

TEST(ComputeSenderIntegration, ParallelPoolMatchesReference) {
    const RenderSettings settings{.width = 64, .height = 48, .max_iterations = 100, .escape_radius = 2.0};
    const ViewPort viewport{-0.75, -0.74, 0.10, 0.11};

    exec::static_thread_pool pool{4};
    auto sched = pool.get_scheduler();

    FrameBuffer fb = FrameBuffer::Make(settings.width, settings.height);
    ex::sync_wait(ex::on(sched, ex::just(&fb) | MakeComputeSender(settings, viewport)));

    EXPECT_EQ(fb.rgba, RenderReference(settings, viewport));
}

TEST(ComputeSenderIntegration, DifferentViewportsProduceDifferentImages) {
    const RenderSettings settings{.width = 32, .height = 32, .max_iterations = 100, .escape_radius = 2.0};

    FrameBuffer fb_a = FrameBuffer::Make(settings.width, settings.height);
    FrameBuffer fb_b = FrameBuffer::Make(settings.width, settings.height);

    ex::sync_wait(ex::just(&fb_a) | MakeComputeSender(settings, AppState::INITIAL_VIEWPORT));
    ex::sync_wait(ex::just(&fb_b) | MakeComputeSender(settings, ViewPort{-0.745, -0.743, 0.131, 0.133}));

    EXPECT_NE(fb_a.rgba, fb_b.rgba);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
