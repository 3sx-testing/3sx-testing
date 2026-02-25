Device + OS: RK3566 / RockNIX (Wayland/Sway), libmali

Working folder layout (bin/, lib/, launcher)

Required runtime env vars (the key part):

SDL_VIDEODRIVER=wayland

SDL_RENDER_DRIVER=opengles2 ✅ (this was the fix)

LD_LIBRARY_PATH=$PORTDIR/lib

HOME=/storage

SDL_JOYSTICK_HIDAPI=0

Where SF33RD.AFS must live (and how it’s discovered)

“Known crash” symptoms and the proof:

SIGSEGV in Wayland_GLES_MakeCurrent inside SDL3

How you debugged:

ldd, strace, enabling core dumps, gdb corefile

Exact commands to reproduce

=============================

✅ What Changed in sdl_app.c

You made two safety fixes to prevent undefined behavior when SDL3 renderer selection changes (which is exactly what happened on RockNIX).

🔧 Change #1 — screen_texture_scale_mode() Default Case
❌ Original
static SDL_ScaleMode screen_texture_scale_mode() {
    switch (scale_mode) {
    case SCALEMODE_LINEAR:
    case SCALEMODE_SOFT_LINEAR:
        return SDL_SCALEMODE_LINEAR;

    case SCALEMODE_NEAREST:
    case SCALEMODE_SQUARE_PIXELS:
    case SCALEMODE_INTEGER:
        return SDL_SCALEMODE_NEAREST;
    }
}

There is no default return.

If scale_mode ever contains an unexpected value (corruption, config issue, enum expansion, etc.), the function returns undefined memory → UB → potential crash.

✅ Current (Safe)
static SDL_ScaleMode screen_texture_scale_mode() {
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
Why This Matters

On RockNIX:

SDL renderer creation order changed

Renderer teardown path was triggered

Any UB during renderer initialization can amplify instability

This change eliminates one potential UB source.

🔧 Change #2 — get_letterbox_rect() Default Case
❌ Original
static SDL_FRect get_letterbox_rect(int win_w, int win_h) {
    switch (scale_mode) {
    case SCALEMODE_NEAREST:
    case SCALEMODE_LINEAR:
    case SCALEMODE_SOFT_LINEAR:
        return fit_4_by_3_rect(win_w, win_h);

    case SCALEMODE_INTEGER:
        return fit_integer_rect(win_w, win_h, 7, 9);

    case SCALEMODE_SQUARE_PIXELS:
        return fit_integer_rect(win_w, win_h, 1, 1);
    }
}

Again — no default return → undefined behavior if enum value is invalid.

✅ Current (Safe)
default:
    // Safe fallback: don't return garbage; use standard 4:3 fit.
    return fit_4_by_3_rect(win_w, win_h);
Why This Matters

Returning an uninitialized SDL_FRect would absolutely cause:

rendering corruption

possible invalid memory usage

or even GPU driver crashes

On embedded drivers (libmali), that can segfault fast.

⚠️ Important: What Actually Fixed the Crash

These code changes improved safety — but the real crash fix was:

SDL_RENDER_DRIVER=opengles2

The crash stack clearly showed:

Wayland_GLES_MakeCurrent()
SDL_GL_DestroyContext_REAL()
SDL_CreateRendererWithProperties_REAL()

So SDL3 was:

Trying a renderer backend

Failing

Destroying context

Hitting a bad path inside Wayland + libmali

Forcing opengles2 stopped SDL from entering the bad renderer path.

📄 How You Should Document This in the Repo

Create a file:

docs/rocknix-rk3566.md

Put something like:

RockNIX / RK3566 (Anbernic RG ARC) Notes
Required Runtime Environment

The following environment variables are required to avoid SDL3 Wayland crashes on libmali:

SDL_VIDEODRIVER=wayland
SDL_RENDER_DRIVER=opengles2
LD_LIBRARY_PATH=<port>/lib
HOME=/storage
SDL_JOYSTICK_HIDAPI=0
Known Crash (If Not Set)

Without SDL_RENDER_DRIVER=opengles2, SDL3 may crash during startup:

Wayland_GLES_MakeCurrent()
SDL_GL_DestroyContext_REAL()
SDL_CreateRendererWithProperties_REAL()

Segfault at address 0x10.

This is caused by SDL3 attempting an incompatible renderer backend on Wayland + libmali.

Source Code Safety Fixes

Two defensive changes were added to port/sdl/sdl_app.c:

Added default: return in screen_texture_scale_mode()

Added default: return in get_letterbox_rect()

These prevent undefined behavior if scale_mode contains unexpected values.

🧠 Big Picture

Your issue was:

Not FFmpeg

Not AFS path

Not libcdio

Not compiler flags

Not outline atomics

Not GLIBCXX mismatch

It was:

SDL3 renderer backend auto-selection + Wayland + libmali interaction.

And you now have:

A reproducible fix

A documented workaround

A stable launcher

Cleaner enum safety in your code


===============
