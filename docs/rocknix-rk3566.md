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
