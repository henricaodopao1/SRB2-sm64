# SRB2 + libsm64 Integration Log

## Read This First

This file is for the next AI taking over this branch.

Assume nothing.
Do not trust old assumptions.
Do not trust "that sounds likely".
Use the facts below first.

At the end of the latest session, the state is:

- Mario can be enabled in-game.
- Mario uses `libsm64` movement and camera-relative input correctly.
- Mario renders as a 3D model in OpenGL instead of the 2D Sonic sprite.
- Face/eyes/hat front texture is CORRECT.
- Bridge collision blocker is FIXED.
- Object interaction is IMPLEMENTED (rings, enemies, monitors).
- **Springs are still too weak** — Mario cannot pass Greenflower Zone using springs.
- **Audio works** — SM64 audio is playing correctly.

Do not waste time re-fixing movement.
Do not waste time re-fixing the renderer.
Do not waste time re-fixing the geometry exporter.
Do not waste time re-fixing rings/monitors/enemy interaction — those work now.

The remaining work is:

1. **Fix spring launch strength** — diagnosis below, root cause identified but not yet fully resolved.
2. ~~**Fix or re-implement Mario audio**~~ — **DONE. Audio works.**

---

## High-Level Goal

Integrate `libsm64` into SRB2 so the player can control Mario with authentic SM64 movement and see the actual Mario model rendered inside SRB2's OpenGL renderer.

---

## How To Build

`make MINGW64=1 -j4` is broken in the current MSYS2 environment due to a temp file issue where the native MinGW GCC cannot create temporary files when invoked through make's shell context. The root cause is a Windows/MSYS2 path mismatch for TMP/TEMP when make spawns subprocesses.

**Workaround — do this every build:**

Run these three commands from an MSYS2 bash shell with PATH set:

```bash
export PATH=/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH
cd /c/Users/henri/SRB2/src
```

Step 1 — compile p_sm64.c (only needed if you changed it):
```bash
cc -DCOMPVERSION -g -fwrapv -DSTDC_HEADERS -DUSE_WGL_SWAP -DDIRECTFULLSCREEN -DHAVE_SDL -DHAVE_MIXER -DHAVE_MIXERX -I../libs/SDL2/x86_64-w64-mingw32/include/SDL2 -I../libs/SDLMixerX/x86_64-w64-mingw32/include/SDL2 -Dmain=SDL_main -DSDLMAIN -DHWRENDER -I../libs/libpng-src -DHAVE_PNG -I../libs/curl/include -DHAVE_CURL -I../libs/miniupnpc/include -DMINIUPNP_STATICLIB -DHAVE_MINIUPNPC -I../libs/gme/include -DHAVE_GME -I../libs/libopenmpt/inc -DHAVE_OPENMPT -I../libs/zlib -DHAVE_ZLIB -march=nocona -O3 -DNDEBUG -I../libsm64/src -Wall -Wno-trigraphs -W -Wno-div-by-zero -Wfloat-equal -Wundef -Wendif-labels -Wshadow -Wpointer-arith -Wbad-function-cast -Wcast-qual -Wcast-align -Wwrite-strings -Wsign-compare -Wno-error=address-of-packed-member -Wlogical-op -Waggregate-return -Wold-style-definition -Wmissing-prototypes -Wmissing-declarations -Wmissing-field-initializers -Wmissing-noreturn -Wnested-externs -Winline -funit-at-a-time -Wlogical-op -Wdisabled-optimization -Wformat-y2k -Wno-error=format-overflow=2 -Wformat-security -Wno-suggest-attribute=noreturn -Wno-error=suggest-attribute=noreturn -Wno-logical-op -Wno-error=logical-op -Wno-tautological-compare -Wno-error=tautological-compare -Wimplicit-fallthrough=4 -Wno-error=format-overflow -Wno-format-overflow -Wno-error=multistatement-macros -c -o ../make/mingw/64/SDL/objs/p_sm64.o p_sm64.c
```

Step 2 — compile comptime.c (always needed, updates build timestamp):
```bash
bash ../comptime.sh . && cc -DCOMPVERSION -g -fwrapv -DSTDC_HEADERS -DUSE_WGL_SWAP -DDIRECTFULLSCREEN -DHAVE_SDL -DHAVE_MIXER -DHAVE_MIXERX -I../libs/SDL2/x86_64-w64-mingw32/include/SDL2 -I../libs/SDLMixerX/x86_64-w64-mingw32/include/SDL2 -Dmain=SDL_main -DSDLMAIN -DHWRENDER -I../libs/libpng-src -DHAVE_PNG -I../libs/curl/include -DHAVE_CURL -I../libs/miniupnpc/include -DMINIUPNP_STATICLIB -DHAVE_MINIUPNPC -I../libs/gme/include -DHAVE_GME -I../libs/libopenmpt/inc -DHAVE_OPENMPT -I../libs/zlib -DHAVE_ZLIB -march=nocona -O3 -DNDEBUG -I../libsm64/src -Wall -c -o ../make/mingw/64/SDL/objs/comptime.o comptime.c
```

Step 3 — link (always needed):
```bash
cc -o ../bin/srb2win64.exe.debug ../make/mingw/64/SDL/objs/sdl/i_net.o ../make/mingw/64/SDL/objs/sdl/i_system.o ../make/mingw/64/SDL/objs/sdl/i_main.o ../make/mingw/64/SDL/objs/sdl/i_video.o ../make/mingw/64/SDL/objs/sdl/dosstr.o ../make/mingw/64/SDL/objs/sdl/endtxt.o ../make/mingw/64/SDL/objs/sdl/hwsym_sdl.o ../make/mingw/64/SDL/objs/sdl/ogl_sdl.o ../make/mingw/64/SDL/objs/sdl/mixer_sound.o ../make/mingw/64/SDL/objs/sdl/i_threads.o ../make/mingw/64/SDL/objs/hardware/hw_bsp.o ../make/mingw/64/SDL/objs/hardware/hw_draw.o ../make/mingw/64/SDL/objs/hardware/hw_light.o ../make/mingw/64/SDL/objs/hardware/hw_main.o ../make/mingw/64/SDL/objs/hardware/hw_clip.o ../make/mingw/64/SDL/objs/hardware/hw_md2.o ../make/mingw/64/SDL/objs/hardware/hw_cache.o ../make/mingw/64/SDL/objs/hardware/hw_md2load.o ../make/mingw/64/SDL/objs/hardware/hw_md3load.o ../make/mingw/64/SDL/objs/hardware/hw_model.o ../make/mingw/64/SDL/objs/hardware/hw_batching.o ../make/mingw/64/SDL/objs/hardware/hw_shaders.o ../make/mingw/64/SDL/objs/hardware/r_opengl/r_opengl.o ../make/mingw/64/SDL/objs/md5.o ../make/mingw/64/SDL/objs/apng.o ../make/mingw/64/SDL/objs/./string.o ../make/mingw/64/SDL/objs/./d_main.o ../make/mingw/64/SDL/objs/./dehacked.o ../make/mingw/64/SDL/objs/./deh_soc.o ../make/mingw/64/SDL/objs/./deh_lua.o ../make/mingw/64/SDL/objs/./deh_tables.o ../make/mingw/64/SDL/objs/./z_zone.o ../make/mingw/64/SDL/objs/./f_finale.o ../make/mingw/64/SDL/objs/./f_wipe.o ../make/mingw/64/SDL/objs/./g_demo.o ../make/mingw/64/SDL/objs/./g_game.o ../make/mingw/64/SDL/objs/./g_input.o ../make/mingw/64/SDL/objs/./am_map.o ../make/mingw/64/SDL/objs/./command.o ../make/mingw/64/SDL/objs/./console.o ../make/mingw/64/SDL/objs/./hu_stuff.o ../make/mingw/64/SDL/objs/./i_time.o ../make/mingw/64/SDL/objs/./y_inter.o ../make/mingw/64/SDL/objs/./st_stuff.o ../make/mingw/64/SDL/objs/./m_aatree.o ../make/mingw/64/SDL/objs/./m_anigif.o ../make/mingw/64/SDL/objs/./m_argv.o ../make/mingw/64/SDL/objs/./m_bbox.o ../make/mingw/64/SDL/objs/./m_cheat.o ../make/mingw/64/SDL/objs/./m_cond.o ../make/mingw/64/SDL/objs/./m_easing.o ../make/mingw/64/SDL/objs/./m_fixed.o ../make/mingw/64/SDL/objs/./m_menu.o ../make/mingw/64/SDL/objs/./m_misc.o ../make/mingw/64/SDL/objs/./m_perfstats.o ../make/mingw/64/SDL/objs/./m_random.o ../make/mingw/64/SDL/objs/./m_tokenizer.o ../make/mingw/64/SDL/objs/./m_queue.o ../make/mingw/64/SDL/objs/./m_vector.o ../make/mingw/64/SDL/objs/./info.o ../make/mingw/64/SDL/objs/./p_ceilng.o ../make/mingw/64/SDL/objs/./p_enemy.o ../make/mingw/64/SDL/objs/./p_floor.o ../make/mingw/64/SDL/objs/./p_inter.o ../make/mingw/64/SDL/objs/./p_lights.o ../make/mingw/64/SDL/objs/./p_map.o ../make/mingw/64/SDL/objs/./p_maputl.o ../make/mingw/64/SDL/objs/./p_mobj.o ../make/mingw/64/SDL/objs/./p_polyobj.o ../make/mingw/64/SDL/objs/./p_saveg.o ../make/mingw/64/SDL/objs/./p_setup.o ../make/mingw/64/SDL/objs/./p_sight.o ../make/mingw/64/SDL/objs/./p_spec.o ../make/mingw/64/SDL/objs/./p_telept.o ../make/mingw/64/SDL/objs/./p_tick.o ../make/mingw/64/SDL/objs/./p_user.o ../make/mingw/64/SDL/objs/./p_slopes.o ../make/mingw/64/SDL/objs/./p_sm64.o ../make/mingw/64/SDL/objs/./tables.o ../make/mingw/64/SDL/objs/./r_bsp.o ../make/mingw/64/SDL/objs/./r_data.o ../make/mingw/64/SDL/objs/./r_draw.o ../make/mingw/64/SDL/objs/./r_fps.o ../make/mingw/64/SDL/objs/./r_main.o ../make/mingw/64/SDL/objs/./r_plane.o ../make/mingw/64/SDL/objs/./r_segs.o ../make/mingw/64/SDL/objs/./r_skins.o ../make/mingw/64/SDL/objs/./r_sky.o ../make/mingw/64/SDL/objs/./r_splats.o ../make/mingw/64/SDL/objs/./r_things.o ../make/mingw/64/SDL/objs/./r_bbox.o ../make/mingw/64/SDL/objs/./r_textures.o ../make/mingw/64/SDL/objs/./r_translation.o ../make/mingw/64/SDL/objs/./r_patch.o ../make/mingw/64/SDL/objs/./r_patchrotation.o ../make/mingw/64/SDL/objs/./r_picformats.o ../make/mingw/64/SDL/objs/./r_portal.o ../make/mingw/64/SDL/objs/./screen.o ../make/mingw/64/SDL/objs/./taglist.o ../make/mingw/64/SDL/objs/./v_video.o ../make/mingw/64/SDL/objs/./s_sound.o ../make/mingw/64/SDL/objs/./sounds.o ../make/mingw/64/SDL/objs/./w_wad.o ../make/mingw/64/SDL/objs/./filesrch.o ../make/mingw/64/SDL/objs/./lzf.o ../make/mingw/64/SDL/objs/./b_bot.o ../make/mingw/64/SDL/objs/./u_list.o ../make/mingw/64/SDL/objs/./snake.o ../make/mingw/64/SDL/objs/./lua_script.o ../make/mingw/64/SDL/objs/./lua_baselib.o ../make/mingw/64/SDL/objs/./lua_mathlib.o ../make/mingw/64/SDL/objs/./lua_hooklib.o ../make/mingw/64/SDL/objs/./lua_consolelib.o ../make/mingw/64/SDL/objs/./lua_infolib.o ../make/mingw/64/SDL/objs/./lua_mobjlib.o ../make/mingw/64/SDL/objs/./lua_playerlib.o ../make/mingw/64/SDL/objs/./lua_skinlib.o ../make/mingw/64/SDL/objs/./lua_thinkerlib.o ../make/mingw/64/SDL/objs/./lua_maplib.o ../make/mingw/64/SDL/objs/./lua_taglib.o ../make/mingw/64/SDL/objs/./lua_polyobjlib.o ../make/mingw/64/SDL/objs/./lua_blockmaplib.o ../make/mingw/64/SDL/objs/./lua_hudlib.o ../make/mingw/64/SDL/objs/./lua_hudlib_drawlist.o ../make/mingw/64/SDL/objs/./lua_inputlib.o ../make/mingw/64/SDL/objs/./lua_colorlib.o ../make/mingw/64/SDL/objs/blua/lapi.o ../make/mingw/64/SDL/objs/blua/lbaselib.o ../make/mingw/64/SDL/objs/blua/ldo.o ../make/mingw/64/SDL/objs/blua/lfunc.o ../make/mingw/64/SDL/objs/blua/linit.o ../make/mingw/64/SDL/objs/blua/liolib.o ../make/mingw/64/SDL/objs/blua/llex.o ../make/mingw/64/SDL/objs/blua/lmem.o ../make/mingw/64/SDL/objs/blua/lobject.o ../make/mingw/64/SDL/objs/blua/lstate.o ../make/mingw/64/SDL/objs/blua/lstrlib.o ../make/mingw/64/SDL/objs/blua/ltablib.o ../make/mingw/64/SDL/objs/blua/lundump.o ../make/mingw/64/SDL/objs/blua/lzio.o ../make/mingw/64/SDL/objs/blua/lauxlib.o ../make/mingw/64/SDL/objs/blua/lcode.o ../make/mingw/64/SDL/objs/blua/ldebug.o ../make/mingw/64/SDL/objs/blua/ldump.o ../make/mingw/64/SDL/objs/blua/lgc.o ../make/mingw/64/SDL/objs/blua/lopcodes.o ../make/mingw/64/SDL/objs/blua/lparser.o ../make/mingw/64/SDL/objs/blua/lstring.o ../make/mingw/64/SDL/objs/blua/ltable.o ../make/mingw/64/SDL/objs/blua/ltm.o ../make/mingw/64/SDL/objs/blua/lvm.o ../make/mingw/64/SDL/objs/blua/loslib.o ../make/mingw/64/SDL/objs/netcode/d_clisrv.o ../make/mingw/64/SDL/objs/netcode/server_connection.o ../make/mingw/64/SDL/objs/netcode/client_connection.o ../make/mingw/64/SDL/objs/netcode/tic_command.o ../make/mingw/64/SDL/objs/netcode/net_command.o ../make/mingw/64/SDL/objs/netcode/gamestate.o ../make/mingw/64/SDL/objs/netcode/commands.o ../make/mingw/64/SDL/objs/netcode/d_net.o ../make/mingw/64/SDL/objs/netcode/d_netcmd.o ../make/mingw/64/SDL/objs/netcode/d_netfil.o ../make/mingw/64/SDL/objs/netcode/http-mserv.o ../make/mingw/64/SDL/objs/netcode/i_tcp.o ../make/mingw/64/SDL/objs/netcode/mserv.o ../make/mingw/64/SDL/objs/comptime.o ../make/mingw/64/SDL/objs/win32/Srb2win.res -Wl,--disable-dynamicbase -ladvapi32 -lkernel32 -lmsvcrt -luser32 -lws2_32 -lSDL2_mixer_ext -L../libs/SDL2/x86_64-w64-mingw32/lib -L../libs/SDLMixerX/x86_64-w64-mingw32/lib -lmingw32 -lSDL2main -lSDL2 -mwindows -L../libs/libpng-src/projects -lpng64 -L../libs/curl/lib64 -lcurl -L../libs/miniupnpc/mingw64 -lminiupnpc -lws2_32 -liphlpapi -L../libs/gme/win64 -lgme -L../libs/libopenmpt/lib/x86_64/mingw -lopenmpt -L../libs/zlib/win32 -lz64 -L../libsm64/dist -lsm64

objcopy --strip-debug ../bin/srb2win64.exe.debug ../bin/srb2win64.exe
```

If you changed files other than p_sm64.c (e.g. hw_main.c, hw_bsp.c), compile those .o files the same way, substituting the filename. All object files go into `../make/mingw/64/SDL/objs/` (or the subdirectory matching the source path).

Run the game:
```powershell
.\bin\srb2win64.exe -opengl
```

In-game:
```text
sm64_enable 1
```

---

## What Exists Right Now

### 1. libsm64 is already integrated into the build

- `src/p_sm64.c` — main bridge file
- `src/p_sm64.h` — public API
- Links against `-lsm64` from `libsm64/dist/`
- Runtime expects `baserom.us.z64` in the game directory

### 2. Player-side libsm64 bridge

Main responsibilities in `src/p_sm64.c`:

- initialization of libsm64 with ROM
- coordinate conversion between SRB2 and SM64
- one Mario instance per player (up to `MAXPLAYERS`)
- feeding inputs into `sm64_mario_tick`
- reading resulting Mario position/state back into SRB2
- storing per-player geometry buffers for rendering
- static collision surface upload

Very important constants and conventions:

- `SM64_SCALE = 3.333333f`
- SRB2 coordinates: X=horizontal, Y=horizontal, Z=vertical
- SM64 coordinates: X=horizontal, Y=vertical, Z=horizontal

Conversion used everywhere in the bridge:

- `SM64.X = SRB2.X`
- `SM64.Y = SRB2.Z`
- `SM64.Z = -SRB2.Y`

The minus sign on SRB2 Y is load-bearing. Do not remove it.

Helper macros (defined at top of p_sm64.c):

```c
#define Doom2SM64X(x)  (FIXED_TO_FLOAT(x) * SM64_SCALE)
#define Doom2SM64Y(z)  (FIXED_TO_FLOAT(z) * SM64_SCALE)
#define Doom2SM64Z(y)  (-FIXED_TO_FLOAT(y) * SM64_SCALE)
```

### 3. Input mapping

Already correct. Do not touch.

- `inputs.stickX = (float)player->cmd.sidemove / 50.0f;`
- `inputs.stickY = -(float)player->cmd.forwardmove / 50.0f;`
- For local players, camera angle uses `P_GetLocalAngle(player)`, not `player->mo->angle`

### 4. Mario 3D rendering

File: `src/hardware/hw_main.c`

- Intercepts sprite rendering when `player->sm64_active` is true
- Draws Mario mesh from `libsm64` geometry buffers
- Face/eyes/hat texture is CORRECT in the current build
- Ghosting (Mario rendering behind walls he's standing in front of) was fixed by applying interpolated camera-relative position offset in `HWR_DrawSM64Mario`

### 5. Level collision export

File: `src/hardware/hw_bsp.c`

- Exports SM64 static surfaces from SRB2 map geometry
- Triangulates floors, ceilings, walls from subsectors/segs
- Slope-aware heights using `P_GetZAt(...)`
- The Greenflower bridge invisible wall bug is FIXED

---

## Object Interaction — IMPLEMENTED

The function `P_SM64_ObjectInteraction` in `src/p_sm64.c` is called at the end of `P_SM64_Tick` every frame.

### Architecture

Mario's mobj gets `MF_NOCLIPTHING` set at the start of every call. This prevents SRB2's normal collision system (`PIT_CheckThing` in `p_map.c`) from processing badnik collisions against Mario's mobj. Without this, when a badnik moves via `P_TryMove` and touches Mario's mobj, SRB2 would fire `P_TouchSpecialThing(badnik, mario_mobj)` which calls `P_DamageMobj`/`P_KillMobj` on the player — permanently corrupting health state. The interaction function handles everything manually instead.

Detection range: `mo->radius + 64*FRACUNIT` in XY, with a vertical range of `8*FRACUNIT` above the object top and `24*FRACUNIT` below the object bottom.

### Springs (MF_SPRING)

- Calls `P_DoSpring(thing, mo)` which sets `mo->momz` to the spring value and adjusts `mo->z` above the spring
- Saves position and converts momentum to SM64 velocity
- After the loop: calls `sm64_set_mario_position` FIRST (teleport to spring top), then `sm64_set_mario_velocity` (apply launch velocity), then `sm64_set_mario_action(marioId, SM64_ACT_FREEFALL)`
- The position-before-velocity order is critical. Without it, gravity cancels the impulse in the same tick.
- **STILL BROKEN: spring launch is too weak to pass Greenflower Zone.** See spring bug section below.

### Enemies/Badniks (MF_ENEMY|MF_BOSS)

- Calls `sm64_mario_attack(marioId, ex, ey, ez, hitbox_h)` to check if Mario is attacking that object
- If yes: `P_DamageMobj(thing, mo, mo, 1, 0)` — kills/damages the badnik
- If no AND Mario has no invincibility/flashing: calls `sm64_mario_take_damage` (SM64 internal knockback/animation), then `P_PlayerRingBurst` + sets `player->powers[pw_flashing] = flashingtics`
- `pw_flashing` is decremented manually at the end of `P_SM64_Tick` because `P_PlayerThink` is bypassed for SM64 players (see below)

### Monitors (MF_MONITOR)

- Checks `(state->action & SM64_ACT_FLAG_ATTACKING) || sm64_mario_attack(marioId, ex, ey, ez, hitbox_h)`
- If yes: `P_DamageMobj(thing, mo, mo, 1, 0)` — breaks the monitor
- The `SM64_ACT_FLAG_ATTACKING = (1 << 23)` catches all attacking actions (punch, kick, ground pound) even when angle is outside `sm64_mario_attack`'s narrow arc
- `sm64_mario_attack` alone only catches stomps (INT_HIT_FROM_ABOVE) and punches within 60 degrees

### Collectibles / Rings / Pickups (MF_SPECIAL, not enemy, not monitor)

- Calls `P_TouchSpecialThing(thing, mo, false)` — uses SRB2's normal pickup code
- Enemies have `MF_SPECIAL` too, so the enemy check (`is_enemy`) runs before this branch

### pw_flashing bug fix

`P_PlayerThink` in `p_user.c` is the normal decrement site for `player->powers[pw_flashing]`. SM64 players bypass `P_PlayerThink` early. Without a manual decrement, `pw_flashing` stays at `flashingtics = 3*TICRATE = 105` forever. `P_CanPickupItem` returns false when `pw_flashing > 78`, so rings can never be collected after taking damage.

Fix: at end of `P_SM64_Tick`, after `P_SM64_ObjectInteraction`:
```c
if (player->powers[pw_flashing] && player->powers[pw_flashing] < UINT16_MAX)
    player->powers[pw_flashing]--;
```

This is already in the code.

---

## Spring Bug — Root Cause Identified, Not Fully Fixed

### Symptom

User reports springs are too weak to pass Greenflower Zone.

### What P_DoSpring does

`P_DoSpring(spring, mo)` in SRB2:
1. Sets `mo->z` to the top of the spring (`spring->z + spring->height`)
2. Sets `mo->momz` to the spring's launch momentum (yellow spring = `20*FRACUNIT`)
3. Sets `MFE_SPRUNG` on the spring's eflags

The bridge then reads `mo->momz` and converts it to SM64 velocity:
```c
spring_vy = FIXED_TO_FLOAT(mo->momz) * SM64_SCALE;
// = 20.0 * 3.333 = 66.7 SM64 units/frame
```

Then:
```c
sm64_set_mario_position(...);   // teleport to spring top
sm64_set_mario_velocity(marioId, spring_vx, spring_vy, spring_vz);
sm64_set_mario_action(marioId, SM64_ACT_FREEFALL);
```

### What libsm64 does with the velocity

`sm64_set_mario_velocity` sets `m->vel[0..2]` directly. On the next `sm64_mario_tick`, the engine uses these as current velocity. Mario's normal jump gives about 42 SM64 units/frame vertical velocity. Yellow spring gives 66.7, which is 1.59x a normal jump — should be noticeably high.

### Why it might still feel weak

The issue is not the formula itself. Possible causes:

1. **SM64 gravity immediately counteracts it.** `sm64_set_mario_action(ACT_FREEFALL)` puts Mario in freefall, which applies gravity next tick. If SM64 doesn't treat the velocity as an initial impulse but immediately applies gravity in the same tick, the effective launch is reduced.

2. **`ACT_FREEFALL` is wrong.** SM64 uses specific actions for being launched upward. `ACT_FREEFALL` (0x0100088C) is for downward freefall. Consider using `ACT_JUMP` (0x03000880) or `ACT_TRIPLE_JUMP` (0x03000882) instead — these may preserve upward velocity better.

3. **The action overwrites internal velocity calculations.** Some SM64 actions recalculate velocity from `forwardVel` on the first frame. Try calling `sm64_set_mario_forward_velocity(marioId, 0)` before setting velocity to prevent this.

### What to try next

Option A — try a different action:
```c
#define SM64_ACT_JUMP         0x03000880
sm64_set_mario_action(marioId, SM64_ACT_JUMP);
```

Option B — multiply the launch velocity by a scale factor to compensate:
```c
spring_vy = FIXED_TO_FLOAT(mo->momz) * SM64_SCALE * 2.5f; // empirical
```

Option C — use `sm64_set_mario_action_arg` with a jump action that has upward velocity encoded.

Do not re-trigger the spring repeatedly. `MFE_SPRUNG` is cleared by `P_MobjThinker` every tick (in `p_mobj.c` around line 10163: `eflags &= ~(MFE_PUSHED|MFE_SPRUNG)`). This used to cause the spring to fire every tick when the Z tolerance was too large (128*FRACUNIT). The current tolerance is `8*FRACUNIT` above spring top, which prevents re-triggering.

---

## Audio System — WORKING

SM64 audio was fixed in a previous session. It is playing correctly in-game.

The implementation lives in `src/p_sm64.c` (search for `#if defined(HAVE_MIXER)`).

Do not touch the audio system. It works.

---

## Full Status At End Of This Session

### Working

- Build (with manual workaround — see build section)
- Game launches
- `sm64_enable 1` works
- Mario physics active
- Mario position updates correctly
- Camera-relative movement correct
- Mario model renders in OpenGL
- Body colors correct
- Face/eyes/hat texture correct
- Bridge crossing in Greenflower works
- Killing badniks (punch and stomp)
- Collecting rings (including after taking damage)
- Breaking monitors (stomp and punch)
- Invincibility timer decrements correctly after damage
- Ghost rendering fixed (Mario no longer clips through walls visually)

### Not Working

- Springs too weak (Mario cannot pass Greenflower using springs)
---

## Key Files

| File | Purpose |
|---|---|
| `src/p_sm64.c` | Main bridge: init, tick, object interaction, audio |
| `src/p_sm64.h` | Public API |
| `src/hardware/hw_main.c` | Mario 3D rendering |
| `src/hardware/hw_bsp.c` | Level collision export to SM64 |
| `libsm64/src/libsm64.h` | libsm64 API |
| `libsm64/test/gl33core/gl33core_renderer.c` | Reference renderer (mix formula) |

---

## Important Constants

```c
#define SM64_SCALE      3.333333f
#define flashingtics    (3*TICRATE)   // = 105 ticks
#define SM64_ACT_FLAG_ATTACKING (1 << 23)  // 0x00800000
#define SM64_ACT_FREEFALL       0x0100088C
```

## Important Facts That Are Not Obvious From The Code

- `P_PlayerThink` is bypassed for SM64 players. Any player state that `P_PlayerThink` normally updates (pw_flashing decrement, etc.) must be done manually in `P_SM64_Tick`.
- `MFE_SPRUNG` is cleared every tick by `P_MobjThinker`, not just once after launch.
- Badniks have `MF_ENEMY|MF_SPECIAL`. The `MF_SPECIAL` check for collectibles must come AFTER the enemy check to avoid treating badniks as pickups.
- `sm64_mario_attack` checks `MARIO_PUNCHING` flag AND 60-degree angle arc. It does NOT cover all attacking actions. Use `state->action & SM64_ACT_FLAG_ATTACKING` for a broader check.
- `P_DoSpring` sets `mo->z` above the spring AND sets `mo->momz`. After calling it, zero out `mo->momx/momy/momz` and call `sm64_set_mario_position` before `sm64_set_mario_velocity` — or the velocity will fight against SM64's existing internal state.
