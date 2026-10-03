// ProsperoTV - Headless PC renderer: plays the interface and writes PNG files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// usage: tv_snapshots <fonts dir> <playlist.m3u> <output dir> [width height]
//
// Runs the same interface code as the console build against the PC's stand-in
// keyboard and network, through Mesa's surfaceless EGL, with a fixed 60 Hz
// clock. A script of controller inputs walks through every screen and state;
// the frames it names are written as pictures. A picture from here shows what
// the code draws. It is not a console result.

#include "core/save_file.hpp"
#include "gfx/gl_program.hpp"
#include "gfx/renderer.hpp"
#include "host_platform.hpp"
#include "tv/app.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{

using hui::Action;
using hui::Direction;

bool open_context()
{
    auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display =
        get_platform_display != nullptr
            ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
            : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_API))
        return false;
    const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                                 4,
                                 EGL_CONTEXT_MINOR_VERSION,
                                 5,
                                 EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                 EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                 EGL_NONE};
    EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
    return context != EGL_NO_CONTEXT &&
           eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
}

bool load_font(hui::gfx::Renderer &renderer, const std::string &path, hui::gfx::Font *font,
               hui::ui::FontRef *ref)
{
    std::string data;
    if (!hui::save::read_file(path, &data) || !font->load(data))
    {
        std::fprintf(stderr, "cannot load font %s\n", path.c_str());
        return false;
    }
    ref->font = font;
    ref->texture = renderer.batch().create_font_texture(*font);
    return true;
}

// One step of the walk: an optional change to the stand-in console, one frame
// of input, a wait, and then perhaps a picture.
struct Step
{
    float wait = 0.5f;
    std::uint32_t press = 0;
    Direction nav = Direction::none;
    const char *capture = nullptr;
    void (*before)() = nullptr;
    bool hold = false; // the button stays down for the whole wait
};

constexpr std::uint32_t bit(Action action)
{
    return hui::action_bit(action);
}

Step look(float wait, const char *capture)
{
    return {wait, 0, Direction::none, capture, nullptr};
}
Step press(Action action, float wait = 0.35f, const char *capture = nullptr)
{
    return {wait, bit(action), Direction::none, capture, nullptr};
}
Step move(Direction direction, float wait = 0.3f, const char *capture = nullptr)
{
    return {wait, 0, direction, capture, nullptr};
}
Step hold(Action action, float wait, const char *capture = nullptr)
{
    return {wait, bit(action), Direction::none, capture, nullptr, true};
}
Step change(void (*before)(), float wait = 0.5f, const char *capture = nullptr)
{
    return {wait, 0, Direction::none, capture, before};
}

std::string g_playlist;
ptv::Model *g_model = nullptr;

const Step kWalk[] = {
    // ---- the first launch: nothing saved yet ----
    look(0.5f, "01-first-download"),
    look(1.4f, "02-live-tv"),
    // ---- Live TV ----
    move(Direction::right, 0.16f, "03-live-tv-moving"),
    look(0.7f, "04-live-tv-focus"),
    move(Direction::right, 0.25f),
    press(Action::west, 0.9f, "05-favorite-added"),
    move(Direction::down, 0.3f),
    move(Direction::right, 0.3f),
    press(Action::west, 0.4f),
    press(Action::jump_next, 0.3f),
    press(Action::jump_next, 0.3f),
    press(Action::jump_next, 0.9f, "06-live-tv-scrolled"),
    press(Action::back, 0.8f),
    // ---- the letters: Right from the last column, then up and down ----
    move(Direction::right, 0.2f),
    move(Direction::right, 0.2f),
    move(Direction::right, 0.2f),
    move(Direction::right, 0.3f),
    move(Direction::right, 0.7f, "28-letters"),
    move(Direction::down, 0.2f),
    move(Direction::down, 0.2f),
    move(Direction::down, 0.2f),
    move(Direction::down, 0.2f),
    move(Direction::down, 0.2f),
    move(Direction::down, 0.9f, "29-letter-chosen"),
    move(Direction::left, 0.7f, "30-letter-channels"),
    // ---- R2 held: the pages keep turning ----
    hold(Action::jump_next, 2.4f, "31-pages-held"),
    press(Action::back, 0.8f),
    // ---- the lists of Live TV ----
    move(Direction::up, 0.5f, "07-lists-focused"),
    move(Direction::right, 0.3f),
    move(Direction::right, 0.9f, "08-news"),
    move(Direction::left, 0.4f),
    look(0.6f, "09-recent-empty"),
    press(Action::back, 0.8f),
    move(Direction::down, 0.4f),
    // ---- the search drawer ----
    press(Action::north, 0.9f, "10-search"),
    change([]() { host::set_keyboard_text("sport"); }, 0.1f),
    press(Action::confirm, 0.9f, "11-search-typed"),
    move(Direction::down, 0.3f),
    press(Action::confirm, 0.7f, "12-search-countries"),
    move(Direction::down, 0.2f),
    move(Direction::down, 0.2f),
    press(Action::confirm, 0.8f),
    move(Direction::down, 0.25f),
    move(Direction::down, 0.25f),
    move(Direction::down, 0.3f),
    move(Direction::right, 0.25f),
    move(Direction::right, 0.25f),
    move(Direction::right, 0.8f, "13-search-filters"),
    move(Direction::down, 0.3f),
    press(Action::confirm, 1.0f, "14-search-results"),
    press(Action::north, 0.7f),
    move(Direction::up, 0.25f),
    move(Direction::up, 0.25f),
    move(Direction::up, 0.25f),
    move(Direction::up, 0.25f),
    change([]() { host::set_keyboard_text("zzzz"); }, 0.1f),
    press(Action::confirm, 0.6f),
    press(Action::back, 0.9f, "15-no-match"),
    press(Action::back, 1.0f, "16-search-cleared"),
    // ---- Favorites ----
    press(Action::page_next, 1.1f, "17-favorites"),
    press(Action::west, 0.4f),
    press(Action::west, 1.0f, "18-favorites-empty"),
    // ---- Sources ----
    press(Action::page_next, 1.1f, "19-sources"),
    move(Direction::down, 0.7f, "20-sources-playlist"),
    move(Direction::down, 0.7f, "21-sources-account"),
    move(Direction::up, 0.3f),
    move(Direction::up, 0.3f),
    change([]() { host::set_network(true, g_playlist, 2500); }, 0.1f),
    press(Action::menu, 1.0f, "22-sources-updating"),
    look(2.6f, "23-sources-updated"),
    // ---- Settings ----
    press(Action::page_next, 1.1f, "24-settings"),
    press(Action::confirm, 0.5f),
    move(Direction::down, 0.25f),
    move(Direction::down, 0.7f, "25-settings-changed"),
    move(Direction::up, 0.2f),
    move(Direction::up, 0.2f),
    press(Action::confirm, 0.4f),
    // ---- About ----
    press(Action::page_next, 1.1f, "32-about"),
    // ---- the states that are hard to reach on purpose ----
    press(Action::back, 0.9f),
    change(
        []()
        {
            const iptv::Channel &channel = g_model->channel(g_model->visible(3));
            g_model->report_playback_failure(channel.id.c_str(), channel.name.c_str(), -5, 2,
                                             "The stream did not answer.");
        },
        0.9f, "26-channel-failed"),
    press(Action::back, 0.8f),
    change([]() { host::set_network(false, "", 400); }, 0.1f),
    press(Action::menu, 1.6f, "27-update-failed"),
    look(4.0f, nullptr),
};

} // namespace

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: %s <fonts dir> <playlist.m3u> <output dir> [width height]\n",
                     argv[0]);
        return 2;
    }
    const std::string fonts_dir = argv[1];
    g_playlist = argv[2];
    const std::string output = argv[3];
    const int width = argc > 5 ? std::atoi(argv[4]) : 1920;
    const int height = argc > 5 ? std::atoi(argv[5]) : 1080;

    if (!open_context())
    {
        std::fprintf(stderr, "no surfaceless EGL OpenGL 4.5 context\n");
        return 1;
    }
    std::fprintf(stderr, "GL %s / %s\n", reinterpret_cast<const char *>(glGetString(GL_VERSION)),
                 reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    hui::gfx::set_glsl_prefix("#version 450 core\n");

    hui::gfx::Renderer renderer;
    hui::gfx::Font regular;
    hui::gfx::Font semibold;
    hui::gfx::Font display;
    hui::gfx::Font mono;
    hui::ui::Fonts fonts;
    if (!renderer.init() ||
        !load_font(renderer, fonts_dir + "/inter-regular.huifont", &regular, &fonts.regular) ||
        !load_font(renderer, fonts_dir + "/inter-semibold.huifont", &semibold, &fonts.semibold) ||
        !load_font(renderer, fonts_dir + "/montserrat-medium.huifont", &display, &fonts.display) ||
        !load_font(renderer, fonts_dir + "/dejavu-sans-mono.huifont", &mono, &fonts.mono))
        return 1;
    // Dusk uses four faces; the two slots left are for the scripts channel
    // names need.
    fonts.pixel = fonts.mono;
    fonts.hand = fonts.regular;

    GLuint framebuffer = 0;
    GLuint color = 0;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 1;

    // A console that has never run the app: an empty data folder.
    hui::save::ensure_directory(output);
    const std::string data_dir = output + "/data";
    hui::save::ensure_directory(data_dir);
    for (const char *name :
         {"prosperotv-catalog.sqlite3", "prosperotv-custom-catalog.sqlite3",
          "prosperotv-xtream-catalog.sqlite3", "prosperotv-playback-history.sqlite3",
          "iptv-favorites-v1.bin", "iptv-history-v1.bin", "iptv-custom-source-v1.txt",
          "iptv-active-source-v1.txt", "prosperotv-xtream-v1.txt", "prosperotv-interface-v1.txt"})
        std::remove((data_dir + "/" + name).c_str());

    host::reset();
    host::set_network(true, g_playlist, 600);
    ptv::Model model(data_dir);
    g_model = &model;
    model.open();
    ptv::App app(model, fonts, renderer.glass_texture(), ptv::load_settings(data_dir),
                 "host build");

    ptv::Frame frame;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4));
    stbi_flip_vertically_on_write(1);
    bool ok = true;
    // Renders the frame as it was last recorded.
    const auto render_drawn = [&](const std::string &name)
    {
        renderer.begin();
        renderer.backdrop(frame.backdrop);
        renderer.draw(frame.scene);
        if (frame.glass)
            renderer.glass();
        renderer.draw(frame.overlay);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        renderer.present(framebuffer, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (std::size_t i = 3; i < pixels.size(); i += 4)
            pixels[i] = 255;
        const std::string path = output + "/" + name + ".png";
        ok = stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0 && ok;
        std::fprintf(stderr, "wrote %s: %zu shapes, %zu draw calls, GL error 0x%x\n", path.c_str(),
                     renderer.last_instances(), renderer.last_draw_calls(), glGetError());
    };
    const auto render = [&](const std::string &name)
    {
        app.draw(frame);
        render_drawn(name);
    };

    constexpr float kDt = 1.0f / 60.0f;
    hui::ui::Feedback feedback;
    long frames = 0;
    for (const Step &step : kWalk)
    {
        if (step.before != nullptr)
            step.before();
        const int count = std::max(1, static_cast<int>(step.wait / kDt + 0.5f));
        for (int i = 0; i < count; ++i, ++frames)
        {
            hui::InputFrame input;
            input.connected = true;
            if (i == 0)
            {
                input.pressed = step.press;
                input.held = step.press;
                input.nav = step.nav;
            }
            else if (step.hold)
            {
                input.held = step.press;
            }
            feedback.clear();
            app.update(input, kDt, feedback);
            // The stand-in download runs in real time on its own thread: one
            // simulated frame takes a real one, so both clocks agree.
            std::this_thread::sleep_for(std::chrono::microseconds(16667));
        }
        if (step.capture != nullptr)
            render(step.capture);
    }
    // The tuning screen a channel opens on: halfway through the hand-over,
    // and as the player shows it with its bar part filled.
    {
        const std::string id = g_model->channel(g_model->visible(2)).id;
        app.draw_tuning(frame, id, 0.5f);
        render_drawn("33-tuning-handover");
        app.draw_tuning(frame, id, 1.0f, 0.62f);
        render_drawn("34-tuning");
    }
    std::fprintf(stderr, "walk: %ld frames simulated\n", frames);
    model.close();
    return ok ? 0 : 1;
}
