# SRB2 + libsm64 Integration Log

## Read This First

This file is for the next AI taking over this branch.

Assume nothing.
Do not trust old assumptions.
Do not trust "that sounds likely".
Use the facts below first.

At the end of the latest session, the state is:

- Mario can be enabled in-game.
- `sm64_enable 1` works again with one command.
- Mario uses `libsm64` movement and camera-relative input correctly.
- Mario renders as a 3D model in OpenGL instead of the 2D Sonic sprite.
- Face/eyes/hat front texture is CORRECT.
- Bridge collision blocker is FIXED.
- Object interaction is IMPLEMENTED (rings, enemies, monitors).
- SRB2 owns rings, damage, lives, death, and respawn again.
- Mario keeps his hurt reaction / voice on damage.
- Mario dies as Mario and respawns still as Mario.
- Level exit works again.
- **Audio works** — SM64 audio is playing correctly.
- Current blocker: springs are still not correct.

Do not waste time re-fixing movement.
Do not waste time re-fixing the renderer.
Do not waste time re-fixing the geometry exporter.
Do not waste time re-fixing rings/monitors/enemy interaction — those work now.

---

## High-Level Goal

Integrate `libsm64` into SRB2 so the player can control Mario with authentic SM64 movement and see the actual Mario model rendered inside SRB2's OpenGL renderer.

---

## How To Build

Use the normal repo `make` target from an MSYS2 MinGW64 shell. The old hand-written `cc`/link recipe is dead.

```bash
export PATH=/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH
export TMPDIR=/tmp
export TMP=/tmp
export TEMP=/tmp
cd /c/Users/henri/SRB2
make MINGW64=1 -j4
```

`make` already rebuilds `src/comptime.c` and `src/p_sm64.c` when they change. Don't compile those objects by hand.

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
- Current bridge splits springs like this:
  - pure vertical: `mass != 0 && damage == 0`
  - diagonal: `mass != 0 && damage != 0`
  - pure horizontal: `mass == 0 && damage != 0`
- Pure vertical still uses `SM64_ACT_TRIPLE_JUMP`
- Diagonal and horizontal currently use `SM64_ACT_FREEFALL` + `sm64_set_mario_faceangle(...)` + `sm64_set_mario_velocity(...)`
- There is debug logging in the console right now: `SM64 SPRING ...`
- The position-before-velocity order is critical. Without it, gravity cancels the impulse in the same tick.
- Spring launch is NOT fixed. See current blocker section below.

### Enemies/Badniks (MF_ENEMY|MF_BOSS)

- Calls `sm64_mario_attack(marioId, ex, ey, ez, hitbox_h)` to check if Mario is attacking that object
- If yes: `P_DamageMobj(thing, mo, mo, 1, 0)` — kills/damages the badnik
- If no AND Mario has no invincibility/flashing: calls `P_DamageMobj(mo, thing, thing, 1, 0)` so SRB2 keeps full control of rings, death, and respawn rules
- If that hit starts flashing at exactly `flashingtics`, the bridge immediately nudges it to `flashingtics - 1` and calls `P_DoPityCheck(player)` so Mario can start re-collecting rings on the normal SRB2 timing
- `P_KillMobj` now calls `P_SM64_KillMario(player)` so Mario still plays his SM64 death animation while SRB2 handles the actual death state

### Monitors (MF_MONITOR)

- Checks `(state->action & SM64_ACT_FLAG_ATTACKING) || sm64_mario_attack(marioId, ex, ey, ez, hitbox_h)`
- If yes: `P_DamageMobj(thing, mo, mo, 1, 0)` — breaks the monitor
- The `SM64_ACT_FLAG_ATTACKING = (1 << 23)` catches all attacking actions (punch, kick, ground pound) even when angle is outside `sm64_mario_attack`'s narrow arc
- `sm64_mario_attack` alone only catches stomps (INT_HIT_FROM_ABOVE) and punches within 60 degrees

### Collectibles / Rings / Pickups (MF_SPECIAL, not enemy, not monitor)

- Calls `P_TouchSpecialThing(thing, mo, false)` — uses SRB2's normal pickup code
- Enemies have `MF_SPECIAL` too, so the enemy check (`is_enemy`) runs before this branch

### Damage / flashing timing

SM64 players no longer bypass all of `P_PlayerThink`. The SM64 tick now runs inside `P_PlayerThink`, and vanilla SRB2 bookkeeping like timer / exit / sector checks / flashing decrement still happens.

One extra fix is still in the bridge after enemy contact:

```c
if (P_DamageMobj(mo, thing, thing, 1, 0)
    && mo->health > 0
    && player->powers[pw_flashing] == flashingtics)
{
    player->powers[pw_flashing] = flashingtics - 1;
    P_DoPityCheck(player);
}
```

That is there because the first flashing tick in vanilla SRB2 delays ring pickup too much for Mario unless it is nudged down by one tick immediately.

---

## Spring Launch — CURRENT BLOCKER

This is the main thing still broken.

Current state:

- `sm64_enable 1` is fixed
- level exit is fixed
- death / respawn as Mario is fixed
- damage / flashing timing is fixed
- springs are still wrong

Current spring symptoms seen in-game:

- some springs semi-trap Mario for a moment
- some springs launch him mostly upward instead of the intended direction
- some springs are too weak
- some springs are too strong and can clip Mario through ceilings

Latest clue from live logs:

- the problematic spring path is not "pure vertical"
- diagonal springs have both `thing->info->mass` and `thing->info->damage`
- the bridge now logs `SM64 SPRING ...` with type / angle / velocity / headroom data

If another AI picks this up, start from the live spring logs, not from assumptions.

---

## Audio System — WORKING

SM64 audio was fixed in a previous session. It is playing correctly in-game.

The implementation lives in `src/p_sm64.c` (search for `#if defined(HAVE_MIXER)`).

Do not touch the audio system. It works.

---

## Full Status At End Of This Session

### Working

- Build
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
- Mario hurt animation / voice on non-lethal damage
- Mario death animation on SRB2 death
- Respawn keeps Mario enabled
- Level exit / finishing the stage
- Ghost rendering fixed (Mario no longer clips through walls visually)

### Not Working

- Spring launching is still the blocker
- Problematic springs can launch in the wrong direction, feel weak/strong at random, or clip Mario through ceilings
- `SM64 SPRING` debug logging is still enabled in the console on purpose
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
#define SM64_SPRING_VY_SCALE    5.16f
#define SM64_ACT_TRIPLE_JUMP    0x01000882
#define SM64_ACT_FREEFALL       0x0100088C
```

## Important Facts That Are Not Obvious From The Code

- SM64 no longer bypasses all of `P_PlayerThink`. The SM64 tick runs inside `P_PlayerThink`, but the normal SRB2 movement branch is skipped while Mario is active.
- `player->sm64_enabled` is the persistent "stay as Mario" toggle. `player->sm64_active` just means there is an active libsm64 Mario instance right now.
- SRB2 owns rings, damage, lives, death, and respawn. libsm64 only owns Mario's movement, model, and death animation.
- `MFE_SPRUNG` is cleared every tick by `P_MobjThinker`, not just once after launch.
- Badniks have `MF_ENEMY|MF_SPECIAL`. The `MF_SPECIAL` check for collectibles must come AFTER the enemy check to avoid treating badniks as pickups.
- `sm64_mario_attack` checks `MARIO_PUNCHING` flag AND 60-degree angle arc. It does NOT cover all attacking actions. Use `state->action & SM64_ACT_FLAG_ATTACKING` for a broader check.
- `P_DoSpring` sets `mo->z` above the spring AND sets `mo->momz`. After calling it, zero out `mo->momx/momy/momz` and call `sm64_set_mario_position` before `sm64_set_mario_velocity` — or the velocity will fight against SM64's existing internal state.
- For spring debugging, `thing->info->damage != 0` means the spring has a horizontal component. Pure vertical springs are `damage == 0`. Diagonal springs have both `mass` and `damage`.
