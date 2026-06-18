#pragma once

#include <SFML/Graphics.hpp>
#include <stdexec/execution.hpp>

#include <exception>
#include <utility>

#include "types_core.hpp"

namespace ex = stdexec;

class SfmlEventHandler {
public:
    template <typename Receiver>
    struct OperationState {
        Receiver receiver_;
        sf::RenderWindow &window_;
        RenderSettings render_settings_;
        AppState &state_;

        static constexpr float ZOOM_INTERVAL_MS = 100.0f;

        template <typename R>
        explicit OperationState(R &&r, sf::RenderWindow &window, RenderSettings render_settings, AppState &state)
            : receiver_{std::forward<R>(r)}, window_{window}, render_settings_{render_settings}, state_{state} {}

        OperationState(const OperationState &) = delete;
        OperationState &operator=(const OperationState &) = delete;

        void start() & noexcept {
            try {
                HandleEvents();

                if (state_.should_exit) {
                    ex::set_stopped(std::move(receiver_));
                    return;
                }

                if (state_.auto_zoom_enabled) {
                    HandleAutoZoom();
                }

                ex::set_value(std::move(receiver_));
            } catch (...) {
                ex::set_error(std::move(receiver_), std::current_exception());
            }
        }

    private:
        void HandleEvents() {
            sf::Event event;
            while (window_.pollEvent(event)) {
                switch (event.type) {
                case sf::Event::Closed:
                    state_.should_exit = true;
                    break;

                case sf::Event::KeyPressed:
                    HandleKeyPress(event.key);
                    break;

                case sf::Event::MouseButtonPressed:
                    HandleMousePress(event.mouseButton);
                    break;

                case sf::Event::MouseButtonReleased:
                    HandleMouseRelease(event.mouseButton);
                    break;

                default:
                    break;
                }
            }
        }

        void HandleKeyPress(const sf::Event::KeyEvent &key) {
            switch (key.code) {
            case sf::Keyboard::X:
                state_.auto_zoom_enabled = !state_.auto_zoom_enabled;
                if (state_.auto_zoom_enabled) {
                    state_.zoom_clock.restart();
                }
                break;

            case sf::Keyboard::C:
                state_.viewport = AppState::INITIAL_VIEWPORT;
                state_.auto_zoom_enabled = false;
                state_.need_rerender = true;
                break;

            default:
                break;
            }
        }

        void HandleMousePress(const sf::Event::MouseButtonEvent &mouse) {
            if (mouse.button == sf::Mouse::Left) {
                state_.left_mouse_pressed = true;
                ZoomToPoint(mouse.x, mouse.y, true);
            } else if (mouse.button == sf::Mouse::Right) {
                state_.right_mouse_pressed = true;
                ZoomToPoint(mouse.x, mouse.y, false);
            }
        }

        void HandleMouseRelease(const sf::Event::MouseButtonEvent &mouse) {
            if (mouse.button == sf::Mouse::Left) {
                state_.left_mouse_pressed = false;
            } else if (mouse.button == sf::Mouse::Right) {
                state_.right_mouse_pressed = false;
            }
        }

        void HandleAutoZoom() {
            if (state_.zoom_clock.getElapsedTime().asMilliseconds() < ZOOM_INTERVAL_MS) {
                return;
            }

            constexpr double kZoomFactor = 0.95;
            const double new_width = state_.viewport.width() * kZoomFactor;
            const double new_height = state_.viewport.height() * kZoomFactor;

            state_.viewport.x_min = AppState::AUTO_ZOOM_TARGET_X - new_width / 2.0;
            state_.viewport.x_max = AppState::AUTO_ZOOM_TARGET_X + new_width / 2.0;
            state_.viewport.y_min = AppState::AUTO_ZOOM_TARGET_Y - new_height / 2.0;
            state_.viewport.y_max = AppState::AUTO_ZOOM_TARGET_Y + new_height / 2.0;

            state_.need_rerender = true;
            state_.zoom_clock.restart();
        }

        void ZoomToPoint(int pixel_x, int pixel_y, bool zoom_in, double factor = 0.8) {
            if (state_.zoom_clock.getElapsedTime().asMilliseconds() < ZOOM_INTERVAL_MS) {
                return;
            }

            const double target_x = state_.viewport.x_min +
                                    (static_cast<double>(pixel_x) / render_settings_.width) * state_.viewport.width();
            const double target_y = state_.viewport.y_min +
                                    (static_cast<double>(pixel_y) / render_settings_.height) * state_.viewport.height();

            const double zoom_factor = zoom_in ? factor : (1.0 / factor);
            const double new_width = state_.viewport.width() * zoom_factor;
            const double new_height = state_.viewport.height() * zoom_factor;

            const double rel_x = static_cast<double>(pixel_x) / render_settings_.width;
            const double rel_y = static_cast<double>(pixel_y) / render_settings_.height;

            state_.viewport.x_min = target_x - rel_x * new_width;
            state_.viewport.x_max = state_.viewport.x_min + new_width;
            state_.viewport.y_min = target_y - rel_y * new_height;
            state_.viewport.y_max = state_.viewport.y_min + new_height;

            state_.need_rerender = true;
            state_.zoom_clock.restart();
        }
    };

    using sender_concept = ex::sender_t;

    sf::RenderWindow &window_;
    RenderSettings render_settings_;
    AppState &state_;

    SfmlEventHandler(sf::RenderWindow &window, RenderSettings render_settings, AppState &state)
        : window_{window}, render_settings_{render_settings}, state_{state} {}

    template <class Receiver>
    auto connect(Receiver receiver) -> OperationState<Receiver> {
        return OperationState<Receiver>{std::move(receiver), window_, render_settings_, state_};
    }

    template <class Self, class... Env>
    static consteval auto get_completion_signatures()
        -> ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t(), ex::set_error_t(std::exception_ptr)> {
        return {};
    }
};
