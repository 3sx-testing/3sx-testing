#!/bin/sh
# PortMaster OUTER launcher for 3SX (RockNIX / Anbernic RG ARC)
# - Logs to: /storage/roms/ports/3sx/3sx.log
# - Uses local libs in /storage/roms/ports/3sx/lib
# - Forces Wayland + GLES2 renderer (stable on RK3566/libmali)
# - Writes a per-game SDL controller DB for Z/C mapping

PORTDIR="/storage/roms/ports/3sx"
LOG="$PORTDIR/3sx.log"
BIN="$PORTDIR/bin/3sx"

mkdir -p "$PORTDIR"

log() { echo "$*" >> "$LOG"; }

log ""
log "=== launch $(date) ==="
log "PORTDIR=$PORTDIR"
log "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<unset>}"
log "XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-<unset>}"

cd "$PORTDIR" || { log "ERROR: can't cd to $PORTDIR"; exit 1; }

# Writable HOME for config saves
export HOME="/storage"

# Prefer port-bundled libs
export LD_LIBRARY_PATH="$PORTDIR/lib:$LD_LIBRARY_PATH"

# Wayland
export SDL_VIDEODRIVER=wayland

# Force stable renderer path on RK3566/libmali
export SDL_RENDER_DRIVER=opengles2

# Optional: keep audio from crashing launch; your game can still run
# (auto first; the probe below will try others if needed)
# export SDL_AUDIODRIVER=dummy

# ---- Controller fix (Z/C) + correct default SF3 layout ----
export SDL_JOYSTICK_HIDAPI=0

SDL_GUID="19000000010000002c0a000000010000"
DB="/tmp/gamecontrollerdb_3sx.txt"
cat > "$DB" <<EOF
${SDL_GUID},rg_arc_joypad,a:b1,b:b0,x:b3,y:b4,back:b10,start:b11,leftshoulder:b9,rightshoulder:b5,lefttrigger:b8,righttrigger:b2,dpup:b12,dpdown:b13,dpleft:b14,dpright:b15,platform:Linux,
EOF
export SDL_GAMECONTROLLERCONFIG_FILE="$DB"

log "HOME=$HOME"
log "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
log "SDL_VIDEODRIVER=$SDL_VIDEODRIVER"
log "SDL_RENDER_DRIVER=$SDL_RENDER_DRIVER"
log "SDL_JOYSTICK_HIDAPI=$SDL_JOYSTICK_HIDAPI"
log "SDL_GAMECONTROLLERCONFIG_FILE=$SDL_GAMECONTROLLERCONFIG_FILE"

# ---- Debug helpers ----
# 1) Show dynamic loader resolution (very useful when stuff silently changes)
log "--- ldd (filtered) ---"
( ldd "$BIN" 2>&1 | grep -E "not found|ld-linux|SDL|avcodec|avformat|avutil|swresample|wayland|decor|xkb|drm|gbm|EGL|GLES" ) >> "$LOG" || true

# 2) Dump SDL3 version string if present in the lib
if [ -f "$PORTDIR/lib/libSDL3.so.0" ]; then
  SDL3VER="$(strings "$PORTDIR/lib/libSDL3.so.0" 2>/dev/null | grep -m1 -E '^SDL3 [0-9]+\.[0-9]+\.[0-9]+' )"
  [ -n "$SDL3VER" ] && log "SDL3_LIB_VERSION=$SDL3VER"
fi

# ---- Run with audio probing ----
run_try() {
  driver="$1"
  log "--- trying SDL_AUDIODRIVER=$driver ---"
  if [ "$driver" = "auto" ]; then
    unset SDL_AUDIODRIVER
  else
    export SDL_AUDIODRIVER="$driver"
  fi

  # Important: keep stdout+stderr in the log
  "$BIN" >> "$LOG" 2>&1
  return $?
}

# Try audio backends in order; stop on first success (exit code 0)
run_try auto; RET=$?
if [ $RET -ne 0 ]; then run_try pipewire; RET=$?; fi
if [ $RET -ne 0 ]; then run_try pulseaudio; RET=$?; fi
if [ $RET -ne 0 ]; then run_try alsa; RET=$?; fi
if [ $RET -ne 0 ]; then log "--- forcing SDL_AUDIODRIVER=dummy ---"; export SDL_AUDIODRIVER=dummy; "$BIN" >> "$LOG" 2>&1; RET=$?; fi

log "exit=$RET"
exit $RET
