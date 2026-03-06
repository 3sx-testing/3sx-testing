#include "port/sdl/sdl_app.h"
#include "common.h"

/*
 * Config header moved around across branches.
 * Prefer port/config/config.h when present, otherwise fall back to older port/config.h
 */
#if defined(__has_include)
  #if __has_include("port/config/config.h")
    #include "port/config/config.h"
  #else
    #include "port/config.h"
  #endif
#else
  #include "port/config/config.h"
#endif

#if defined(__has_include)
  #if __has_include("port/config/keymap.h")
    #include "port/config/keymap.h"
    #define HAVE_KEYMAP 1
  #else
    #define HAVE_KEYMAP 0
  #endif
#else
  #include "port/config/keymap.h"
  #define HAVE_KEYMAP 1
#endif

#include "port/sdl/netplay_screen.h"
#include "port/sdl/netstats_renderer.h"
#include "port/sdl/sdl_debug_text.h"
#include "port/sdl/sdl_game_renderer.h"
#include "port/sdl/sdl_message_renderer.h"
#include "port/sdl/sdl_pad.h"
#include "port/sound/adx.h"
#include "sf33rd/AcrSDK/ps2/foundaps2.h"

#include <SDL3/SDL.h>

#define FRAME_END_TIMES_MAX 30

typedef enum ScaleMode {
    SCALEMODE_NEAREST,
    SCALEMODE_LINEAR,
    SCALEMODE_SOFT_LINEAR,
    SCALEMODE_SQUARE_PIXELS,
    SCALEMODE_INTEGER,
} ScaleMode;

static const char* app_name = "Street Fighter III: 3rd Strike";
static const float display_target_ratio = 4.0f / 3.0f;
static const int window_min_width = 384;
static const int window_min_height = (int)(window_min_width / display_target_ratio);
static const Uint64 target_frame_time_ns = (Uint64)(1000000000.0 / TARGET_FPS);

SDL_Window* window = NULL;
static SDL_Renderer* renderer = NULL;
static SDL_Texture* screen_texture = NULL;
static ScaleMode scale_mode = SCALEMODE_SOFT_LINEAR;

static Uint64 frame_deadline = 0;
static Uint64 frame_end_times[FRAME_END_TIMES_MAX];
static int frame_end_times_index = 0;
static bool frame_end_times_filled = false;
static double fps = 0;
static Uint64 frame_counter = 0;

static bool should_save_screenshot = false;
static Uint64 last_mouse_motion_time = 0;
static const int mouse_hide_delay_ms = 2000; // 2 seconds

// Runtime toggles (env):
//   SDLAPP_VSYNC:       "1" (default) or "0"
//   SDLAPP_SLEEP_PACE:  "1" (default) or "0"  (only relevant when vsync is off)
static int get_env_bool_default(const char* name, int def_value) {
    const char* v = SDL_getenv(name);
    if (!v || !*v) {
        return def_value;
    }
    // Accept 0/1, true/false-ish, etc.
    return (SDL_atoi(v) != 0) ? 1 : 0;
}

static SDL_ScaleMode screen_texture_scale_mode(void) {
    switch (scale_mode) {
    case SCALEMODE_LINEAR:
    case SCALEMODE_SOFT_LINEAR:
        return SDL_SCALEMODE_LINEAR;

    case SCALEMODE_NEAREST:
    case SCALEMODE_SQUARE_PIXELS:
    case SCALEMODE_INTEGER:
        return SDL_SCALEMODE_NEAREST;

    default:
        // Safe fallback: nearest matches expected pixel-art behavior and avoids UB.
        return SDL_SCALEMODE_NEAREST;
    }
}

static SDL_Point screen_texture_size(void) {
    SDL_Point size;
    SDL_GetRenderOutputSize(renderer, &size.x, &size.y);

    if (scale_mode == SCALEMODE_SOFT_LINEAR) {
        size.x *= 2;
        size.y *= 2;
    }

    return size;
}

static void create_screen_texture(void) {
    if (screen_texture != NULL) {
        SDL_DestroyTexture(screen_texture);
        screen_texture = NULL;
    }

    const SDL_Point size = screen_texture_size();
    screen_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB32, SDL_TEXTUREACCESS_TARGET, size.x, size.y);
    SDL_SetTextureScaleMode(screen_texture, screen_texture_scale_mode());
}

static void init_scalemode(void) {
    const char* raw_scalemode = Config_GetString(CFG_KEY_SCALEMODE);

    if (raw_scalemode == NULL) {
        return;
    }

    if (SDL_strcmp(raw_scalemode, "nearest") == 0) {
        scale_mode = SCALEMODE_NEAREST;
    } else if (SDL_strcmp(raw_scalemode, "linear") == 0) {
        scale_mode = SCALEMODE_LINEAR;
    } else if (SDL_strcmp(raw_scalemode, "soft-linear") == 0) {
        scale_mode = SCALEMODE_SOFT_LINEAR;
    } else if (SDL_strcmp(raw_scalemode, "square-pixels") == 0) {
        scale_mode = SCALEMODE_SQUARE_PIXELS;
    } else if (SDL_strcmp(raw_scalemode, "integer") == 0) {
        scale_mode = SCALEMODE_INTEGER;
    }
}

int SDLApp_Init(void) {
    Config_Init();

#if HAVE_KEYMAP
    Keymap_Init();
#endif

    init_scalemode();

    // VSync toggle (default ON)
    const int want_vsync = get_env_bool_default("SDLAPP_VSYNC", 1);

    SDL_SetAppMetadata(app_name, "0.1", NULL);
    SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR, "1");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    // Let env decide vsync hint (some backends only look at this)
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, want_vsync ? "1" : "0");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return 1;
    }

    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    if (Config_GetBool(CFG_KEY_FULLSCREEN)) {
        window_flags |= SDL_WINDOW_FULLSCREEN;
    }

    int window_width = Config_GetInt(CFG_KEY_WINDOW_WIDTH);
    if (window_width < window_min_width) {
        window_width = window_min_width;
    }

    int window_height = Config_GetInt(CFG_KEY_WINDOW_HEIGHT);
    if (window_height < window_min_height) {
        window_height = window_min_height;
    }

    if (!SDL_CreateWindowAndRenderer(app_name, window_width, window_height, window_flags, &window, &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return 1;
    }

    // Apply vsync preference explicitly on the renderer
    if (!SDL_SetRenderVSync(renderer, want_vsync)) {
        SDL_Log("SDL_SetRenderVSync(%d) failed: %s", want_vsync, SDL_GetError());
    }

    // Optional: log what we actually got
    {
        int vs = 0;
        if (SDL_GetRenderVSync(renderer, &vs)) {
            SDL_Log("SDL renderer vsync = %d (wanted %d)", vs, want_vsync);
        } else {
            SDL_Log("SDL_GetRenderVSync failed: %s", SDL_GetError());
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Initialize rendering subsystems
    SDLMessageRenderer_Initialize(renderer);
    SDLGameRenderer_Init(renderer);

#if defined(DEBUG)
    SDLDebugText_Initialize(renderer);
#endif

    // Initialize screen texture
    create_screen_texture();

    // Initialize pads
    SDLPad_Init();

    return 0;
}

void SDLApp_Quit(void) {
    Config_Destroy();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

static void set_screenshot_flag_if_needed(SDL_KeyboardEvent* event) {
    if ((event->key == SDLK_GRAVE) && event->down && !event->repeat) {
        should_save_screenshot = true;
    }
}

static void handle_fullscreen_toggle(SDL_KeyboardEvent* event) {
    const bool is_alt_enter = (event->key == SDLK_RETURN) && (event->mod & SDL_KMOD_ALT);
    const bool is_f11 = (event->key == SDLK_F11);
    const bool correct_key = (is_alt_enter || is_f11);

    if (!correct_key || !event->down || event->repeat) {
        return;
    }

    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);

    if (flags & SDL_WINDOW_FULLSCREEN) {
        SDL_SetWindowFullscreen(window, false);
    } else {
        SDL_SetWindowFullscreen(window, true);
    }
}

static void handle_mouse_motion(void) {
    last_mouse_motion_time = SDL_GetTicks();
    SDL_ShowCursor();
}

static void hide_cursor_if_needed(void) {
    const Uint64 now = SDL_GetTicks();

    if ((last_mouse_motion_time > 0) && ((now - last_mouse_motion_time) > (Uint64)mouse_hide_delay_ms)) {
        SDL_HideCursor();
    }
}

bool SDLApp_PollEvents(void) {
    SDL_Event event;
    bool continue_running = true;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
            SDLPad_HandleGamepadDeviceEvent(&event.gdevice);
            break;

        // NOTE:
        // This codebase polls input state every frame via SDLPad_GetButtonState()
        // (SDL_GetKeyboardState / SDL_GetGamepadButton / SDL_GetGamepadAxis),
        // so we do NOT need per-button/per-axis/keyboard event handlers here.
        // Keeping only device connect/disconnect matches the current SDLPad implementation
        // and avoids implicit-decl errors under -Werror.

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            set_screenshot_flag_if_needed(&event.key);
            handle_fullscreen_toggle(&event.key);
            break;

        case SDL_EVENT_MOUSE_MOTION:
            handle_mouse_motion();
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            create_screen_texture();
            break;

        case SDL_EVENT_QUIT:
            continue_running = false;
            break;

        default:
            break;
        }
    }

    return continue_running;
}

void SDLApp_BeginFrame(void) {
    // Clear window
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_SetRenderTarget(renderer, NULL);
    SDL_RenderClear(renderer);

    SDLMessageRenderer_BeginFrame();
    SDLGameRenderer_BeginFrame();
}

static void center_rect(SDL_FRect* rect, int win_w, int win_h) {
    rect->x = (win_w - rect->w) / 2.0f;
    rect->y = (win_h - rect->h) / 2.0f;
}

static SDL_FRect fit_4_by_3_rect(int win_w, int win_h) {
    SDL_FRect rect;
    rect.w = (float)win_w;
    rect.h = (float)win_w / display_target_ratio;

    if (rect.h > (float)win_h) {
        rect.h = (float)win_h;
        rect.w = (float)win_h * display_target_ratio;
    }

    center_rect(&rect, win_w, win_h);
    return rect;
}

static SDL_FRect fit_integer_rect(int win_w, int win_h, int pixel_w, int pixel_h) {
    const int virtual_w = win_w / pixel_w;
    const int virtual_h = win_h / pixel_h;
    const int scale_w = virtual_w / 384;
    const int scale_h = virtual_h / 224;
    int scale = (scale_h < scale_w) ? scale_h : scale_w;

    // Better to show a cropped image than nothing at all
    if (scale < 1) {
        scale = 1;
    }

    SDL_FRect rect;
    rect.w = (float)(scale * 384 * pixel_w);
    rect.h = (float)(scale * 224 * pixel_h);
    center_rect(&rect, win_w, win_h);
    return rect;
}

static SDL_FRect get_letterbox_rect(int win_w, int win_h) {
    switch (scale_mode) {
    case SCALEMODE_NEAREST:
    case SCALEMODE_LINEAR:
    case SCALEMODE_SOFT_LINEAR:
        return fit_4_by_3_rect(win_w, win_h);

    case SCALEMODE_INTEGER:
        // In order to scale a 384x224 buffer to 4:3 we need to stretch vertically by 9 / 7
        return fit_integer_rect(win_w, win_h, 7, 9);

    case SCALEMODE_SQUARE_PIXELS:
        return fit_integer_rect(win_w, win_h, 1, 1);

    default:
        // Safe fallback: don't return garbage; use standard 4:3 fit.
        return fit_4_by_3_rect(win_w, win_h);
    }
}

static void note_frame_end_time(void) {
    frame_end_times[frame_end_times_index] = SDL_GetTicksNS();
    frame_end_times_index = (frame_end_times_index + 1) % FRAME_END_TIMES_MAX;

    if (frame_end_times_index == 0) {
        frame_end_times_filled = true;
    }
}

static void update_fps(void) {
    if (!frame_end_times_filled) {
        return;
    }

    double total_frame_time_ms = 0;

    for (int i = 0; i < FRAME_END_TIMES_MAX - 1; i++) {
        const int cur = (frame_end_times_index + i) % FRAME_END_TIMES_MAX;
        const int next = (cur + 1) % FRAME_END_TIMES_MAX;
        total_frame_time_ms += (double)(frame_end_times[next] - frame_end_times[cur]) / 1e6;
    }

    const double average_frame_time_ms = total_frame_time_ms / (FRAME_END_TIMES_MAX - 1);
    fps = 1000.0 / average_frame_time_ms;
}

static void save_texture(SDL_Texture* texture, const char* filename) {
    SDL_SetRenderTarget(renderer, texture);

    SDL_Surface* rendered_surface = SDL_RenderReadPixels(renderer, NULL);
    if (!rendered_surface) {
        SDL_Log("SDL_RenderReadPixels failed: %s", SDL_GetError());
        return;
    }

    SDL_SaveBMP(rendered_surface, filename);
    SDL_DestroySurface(rendered_surface);
}

static void get_texture_wh(SDL_Texture* texture, int* out_w, int* out_h) {
    float w = 0.0f, h = 0.0f;
    if (!SDL_GetTextureSize(texture, &w, &h)) {
        SDL_Log("SDL_GetTextureSize failed: %s", SDL_GetError());
        *out_w = 0;
        *out_h = 0;
        return;
    }
    *out_w = (int)w;
    *out_h = (int)h;
}

void SDLApp_EndFrame(void) {
    // Run sound processing
    ADX_ProcessTracks();

    // Render (Netplay overlays first, then base frame)
    NetplayScreen_Render();
    NetstatsRenderer_Render();
    SDLGameRenderer_RenderFrame();

    if (should_save_screenshot) {
        save_texture(cps3_canvas, "screenshot_cps3.bmp");
    }

    SDL_SetRenderTarget(renderer, screen_texture);

    // Render window background (black bars)
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // Compute destination rect using actual texture size (SDL3 textures are opaque)
    int tex_w = 0, tex_h = 0;
    get_texture_wh(screen_texture, &tex_w, &tex_h);

    // Render content
    const SDL_FRect dst_rect = get_letterbox_rect(tex_w, tex_h);
    SDL_RenderTexture(renderer, cps3_canvas, NULL, &dst_rect);
    SDL_RenderTexture(renderer, message_canvas, NULL, &dst_rect);

    // Render screen texture to the window
    SDL_SetRenderTarget(renderer, NULL);
    SDL_RenderTexture(renderer, screen_texture, NULL, NULL);

    if (should_save_screenshot) {
        save_texture(screen_texture, "screenshot_screen.bmp");
    }

#if defined(DEBUG)
    SDLDebugText_Render();
#endif

    // Present (vsync boundary when vsync is active)
    SDL_RenderPresent(renderer);

    // Cleanup
    SDLGameRenderer_EndFrame();
    should_save_screenshot = false;

    // Handle cursor hiding
    hide_cursor_if_needed();

    // Only do software pacing if renderer vsync is NOT active.
    int vsync = 0;
    const bool got_vsync = SDL_GetRenderVSync(renderer, &vsync);

    if (!got_vsync || vsync == 0) {
        // Allow disabling sleep-based pacing for lowest latency (tearing possible).
        const int do_sleep_pace = get_env_bool_default("SDLAPP_SLEEP_PACE", 1);

        if (do_sleep_pace) {
            Uint64 now = SDL_GetTicksNS();

            if (frame_deadline == 0) {
                frame_deadline = now + target_frame_time_ns;
            }

            if (now < frame_deadline) {
                const Uint64 sleep_time = frame_deadline - now;
                SDL_DelayNS(sleep_time);
                now = SDL_GetTicksNS();
            }

            frame_deadline += target_frame_time_ns;

            // If we fell behind by more than one frame, resync to avoid spiraling
            if (now > frame_deadline + target_frame_time_ns) {
                frame_deadline = now + target_frame_time_ns;
            }
        } else {
            // No sleep pacing: keep deadline reset so toggling back doesn't jump.
            frame_deadline = 0;
        }
    } else {
        // When vsync is active, don't carry deadline forward (avoid weird jumps if toggling)
        frame_deadline = 0;
    }

    // Measure
    frame_counter += 1;
    note_frame_end_time();
    update_fps();
}

void SDLApp_Exit(void) {
    SDL_Event quit_event;
    quit_event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit_event);
}
