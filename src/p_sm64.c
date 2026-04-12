#include "p_sm64.h"
#include "z_zone.h"
#include "m_argv.h"
#include "console.h"
#include "w_wad.h"
#include "p_local.h"
#include "doomstat.h"
#include "g_game.h"
#include "r_state.h"
#include "r_sky.h"
#include "sounds.h"
#include "s_sound.h"
#include "i_sound.h"

#if defined(HAVE_MIXERX)
#include "SDL_mixer_ext.h"
#elif defined(HAVE_MIXER)
#include <SDL_mixer.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "libsm64.h"

static boolean sm64_initialized = false;
static uint8_t *rom_buffer = NULL;
static uint8_t texture_buffer[4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT];

#define MAX_SURFACES 65536
static struct SM64Surface surfaces[MAX_SURFACES];
static uint32_t num_surfaces = 0;

typedef struct
{
	sm64_surface_debug_kind_t kind;
	INT32 subsector;
	INT32 seg;
	INT32 current_sector;
	INT32 other_sector;
	UINT32 fofflags;
} sm64_surface_debug_info_t;

static sm64_surface_debug_info_t surface_debug_infos[MAX_SURFACES];
static sm64_surface_debug_info_t next_surface_debug_info;

// Mario is ~160 units tall. Sonic is 48.
#define SM64_SCALE 3.333333f

static float Doom2SM64X(fixed_t x) { return ((float)x / 65536.0f) * SM64_SCALE; }
static float Doom2SM64Y(fixed_t z) { return ((float)z / 65536.0f) * SM64_SCALE; }
static float Doom2SM64Z(fixed_t y) { return -((float)y / 65536.0f) * SM64_SCALE; }

static fixed_t SM642DoomX(float x) { return (fixed_t)((x / SM64_SCALE) * 65536.0f); }
static fixed_t SM642DoomY(float z) { return (fixed_t)((-z / SM64_SCALE) * 65536.0f); }
static fixed_t SM642DoomZ(float y) { return (fixed_t)((y / SM64_SCALE) * 65536.0f); }
static float SM642DoomFloatX(float x) { return x / SM64_SCALE; }
static float SM642DoomFloatY(float z) { return -z / SM64_SCALE; }
static float SM642DoomFloatZ(float y) { return y / SM64_SCALE; }

static float mario_pos_buffers[MAXPLAYERS][9 * SM64_GEO_MAX_TRIANGLES];
static float mario_normal_buffers[MAXPLAYERS][9 * SM64_GEO_MAX_TRIANGLES];
static float mario_color_buffers[MAXPLAYERS][9 * SM64_GEO_MAX_TRIANGLES];
static float mario_uv_buffers[MAXPLAYERS][6 * SM64_GEO_MAX_TRIANGLES];
static uint16_t mario_triangle_counts[MAXPLAYERS];

static size_t P_SM64_PlayerIndex(const player_t *player)
{
    return (size_t)(player - players);
}

// ==========================================================================
// MARIO SOUND SYSTEM
// ==========================================================================

// Extract sound bank from soundBits (upper nibble of first byte)
#define SOUND_BANK_FROM_BITS(bits) (((bits) >> 28) & 0xF)
#define SOUND_ID_FROM_BITS(bits) (((bits) >> 16) & 0xFF)

// Sound callback - called by libsm64 when Mario makes a sound
static void P_SM64_PlaySoundCallback(uint32_t soundBits, float *pos)
{
    uint32_t bank = SOUND_BANK_FROM_BITS(soundBits);
    uint32_t soundID = SOUND_ID_FROM_BITS(soundBits);
    mobj_t *mo = NULL;

    // Find which player is making this sound (use position or just pick active Mario)
    // For simplicity, we'll find the first active SM64 player
    size_t i;
    for (i = 0; i < MAXPLAYERS; i++)
    {
        if (players[i].sm64_active && players[i].mo)
        {
            mo = players[i].mo;
            break;
        }
    }

    if (!mo)
        return;

    // Map SM64 sounds to SRB2 sounds
    switch (bank)
    {
        case 0: // SOUND_BANK_ACTION - jumps, steps, landings
            switch (soundID)
            {
                case 0x00: // TERRAIN_JUMP
                case 0x28: // METAL_JUMP
                    S_StartSound(mo, sfx_jump);
                    break;
                case 0x08: // TERRAIN_LANDING
                case 0x29: // METAL_LANDING
                    S_StartSound(mo, sfx_thok); // Landing sound
                    break;
                case 0x10: // TERRAIN_STEP
                case 0x2A: // METAL_STEP
                    // Steps are too frequent, skip or use very quiet sound
                    break;
                case 0x60: // TERRAIN_HEAVY_LANDING
                case 0x2B: // METAL_HEAVY_LANDING
                    S_StartSound(mo, sfx_thok);
                    break;
                case 0x18: // BODY_HIT_GROUND
                    S_StartSound(mo, sfx_gasp); // Oof sound
                    break;
                case 0x45: // BONK (hitting wall)
                    S_StartSound(mo, sfx_gasp);
                    break;
                case 0x35: // THROW
                    // No throw sound in SRB2
                    break;
            }
            break;

        case 1: // SOUND_BANK_MOVING - sliding, riding shell, etc
            switch (soundID)
            {
                case 0x00: // TERRAIN_SLIDE
                    S_StartSound(mo, sfx_skid);
                    break;
            }
            break;

        case 2: // SOUND_BANK_VOICE - Mario's voice (Yahoo, Wah, Hoo)
            switch (soundID)
            {
                case 0x00: // YAH_WAH_HOO (jump voices)
                case 0x04: // YAHOO
                case 0x2B: // YAHOO_WAHA_YIPPEE (triple jump)
                    S_StartSound(mo, sfx_jump);
                    break;
                case 0x0B: // OOOF (taking damage)
                case 0x0A: // ATTACKED
                    S_StartSound(mo, sfx_gasp);
                    break;
                case 0x20: // MAMA_MIA
                    S_StartSound(mo, sfx_gasp);
                    break;
                case 0x08: // WHOA (long jump/takeoff)
                    S_StartSound(mo, sfx_zoom);
                    break;
            }
            break;

        case 3: // SOUND_BANK_GENERAL - coins, doors, etc
            // Skip general sounds - not Mario-specific
            break;

        case 6: // SOUND_BANK_AIR - whoosh sounds
            // Skip or map to zoom
            break;
    }
}

static const char *P_SM64_DebugSurfaceKindName(sm64_surface_debug_kind_t kind)
{
	switch (kind)
	{
		case SM64_SURFACE_DEBUG_SECTOR_FLOOR: return "sector_floor";
		case SM64_SURFACE_DEBUG_SECTOR_CEILING: return "sector_ceiling";
		case SM64_SURFACE_DEBUG_FOF_TOP: return "fof_top";
		case SM64_SURFACE_DEBUG_FOF_BOTTOM: return "fof_bottom";
		case SM64_SURFACE_DEBUG_WALL_ONE_SIDED: return "wall_one_sided";
		case SM64_SURFACE_DEBUG_WALL_FLOOR_STEP: return "wall_floor_step";
		case SM64_SURFACE_DEBUG_WALL_CEILING_GAP: return "wall_ceiling_gap";
		default: return "unknown";
	}
}

static boolean P_SM64_DebugVertsEqual(const INT32 a[3], const INT32 b[3])
{
	return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

static boolean P_SM64_DebugSurfaceMatches(const struct SM64Surface *surface, const struct SM64SurfaceCollisionData *surf)
{
	boolean used[3] = {false, false, false};
	INT32 i;
	const INT32 *query[3] = {surf->vertex1, surf->vertex2, surf->vertex3};

	for (i = 0; i < 3; i++)
	{
		INT32 j;
		boolean matched = false;

		for (j = 0; j < 3; j++)
		{
			if (used[j])
				continue;

			if (P_SM64_DebugVertsEqual(surface->vertices[j], query[i]))
			{
				used[j] = true;
				matched = true;
				break;
			}
		}

		if (!matched)
			return false;
	}

	return true;
}

static INT32 P_SM64_DebugFindSurfaceIndex(const struct SM64SurfaceCollisionData *surf)
{
	UINT32 i;

	if (!surf)
		return -1;

	for (i = 0; i < num_surfaces; i++)
	{
		if (P_SM64_DebugSurfaceMatches(&surfaces[i], surf))
			return (INT32)i;
	}

	return -1;
}

boolean P_SM64_Init(const char *rom_path)
{
    if (sm64_initialized) return true;

    CONS_Printf("SM64: Initializing with ROM %s...\n", rom_path);
    
    FILE *f = fopen(rom_path, "rb");
    if (!f) {
        CONS_Printf("SM64: Failed to open ROM %s\n", rom_path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    rom_buffer = Z_Malloc(size, PU_STATIC, NULL);
    fread(rom_buffer, 1, size, f);
    fclose(f);

    sm64_global_init(rom_buffer, texture_buffer);

    // Initialize SM64 audio system
    sm64_audio_init(rom_buffer);
    CONS_Printf("SM64: Audio system initialized.\n");

    // NOTE: Sound callback disabled - using native SM64 audio streaming instead
    // sm64_register_play_sound_function(P_SM64_PlaySoundCallback);
    CONS_Printf("SM64: Audio streaming enabled (native SM64 sounds).\n");
    sm64_initialized = true;
    return true;
}

static void P_SM64_DestroyAudio(void); // defined below with the audio section

void P_SM64_Shutdown(void)
{
    if (!sm64_initialized) return;
    P_SM64_DestroyAudio();
    sm64_global_terminate();
    if (rom_buffer) Z_Free(rom_buffer);
    rom_buffer = NULL;
    sm64_initialized = false;
}

void P_SM64_LevelSetup(void)
{
    if (!sm64_initialized) return;
    CONS_Printf("SM64: Clearing static surfaces for new level...\n");
    num_surfaces = 0;
    memset(surface_debug_infos, 0, sizeof(surface_debug_infos));
    memset(&next_surface_debug_info, 0, sizeof(next_surface_debug_info));
    memset(mario_triangle_counts, 0, sizeof(mario_triangle_counts));
}

void P_SM64_AddStaticSurface(float s_x1, float s_y1, float s_z1,
                             float s_x2, float s_y2, float s_z2,
                             float s_x3, float s_y3, float s_z3,
                             int terrain_type, boolean is_ceiling)
{
    if (num_surfaces >= MAX_SURFACES) return;

    struct SM64Surface *surf = &surfaces[num_surfaces];
    surface_debug_infos[num_surfaces] = next_surface_debug_info;
    num_surfaces++;
    surf->type = terrain_type;
    surf->force = 0;
    surf->terrain = 1;

    int32_t x1 = (int32_t)(s_x1 * SM64_SCALE);
    int32_t y1 = (int32_t)(s_z1 * SM64_SCALE);
    int32_t z1 = (int32_t)(-s_y1 * SM64_SCALE);

    int32_t x2 = (int32_t)(s_x2 * SM64_SCALE);
    int32_t y2 = (int32_t)(s_z2 * SM64_SCALE);
    int32_t z2 = (int32_t)(-s_y2 * SM64_SCALE);

    int32_t x3 = (int32_t)(s_x3 * SM64_SCALE);
    int32_t y3 = (int32_t)(s_z3 * SM64_SCALE);
    int32_t z3 = (int32_t)(-s_y3 * SM64_SCALE);

    // Compute normal Y in SM64 space
    float ux = (float)(x2 - x1);
    float uz = (float)(z2 - z1);
    float vx = (float)(x3 - x1);
    float vz = (float)(z3 - z1);

    float ny = uz * vx - ux * vz;

    // PROBLEMA FUNDAMENTAL: Toda a transformação de coordenadas SRB2→SM64 inverte as normais
    // SOLUÇÃO: Sempre inverter winding de TODAS as superfícies, independente do tipo
    int32_t tx = x2, ty = y2, tz = z2;
    x2 = x3; y2 = y3; z2 = z3;
    x3 = tx; y3 = ty; z3 = tz;

    surf->vertices[0][0] = x1;
    surf->vertices[0][1] = y1;
    surf->vertices[0][2] = z1;

    surf->vertices[1][0] = x2;
    surf->vertices[1][1] = y2;
    surf->vertices[1][2] = z2;

    surf->vertices[2][0] = x3;
    surf->vertices[2][1] = y3;
    surf->vertices[2][2] = z3;
}

static void P_SM64_LoadSurfaces(void)
{
    if (!sm64_initialized) return;
    sm64_static_surfaces_load(surfaces, num_surfaces);
    CONS_Printf("SM64: Loaded %u static surfaces into libsm64.\n", num_surfaces);
}

static void P_SM64_DebugPrintCollisionSurface(const char *label, const struct SM64SurfaceCollisionData *surf)
{
    INT32 surface_index;

    if (!surf)
    {
        CONS_Printf("%s: <none>\n", label);
        return;
    }

    CONS_Printf(
        "%s: type=%d terrain=%u valid=%u lowerY=%d upperY=%d normal=(%.3f, %.3f, %.3f) origin=%.3f\n",
        label,
        surf->type,
        surf->terrain,
        surf->isValid,
        surf->lowerY,
        surf->upperY,
        surf->normal.x,
        surf->normal.y,
        surf->normal.z,
        surf->originOffset
    );
    CONS_Printf(
        "%s verts(sm64): (%d, %d, %d) (%d, %d, %d) (%d, %d, %d)\n",
        label,
        surf->vertex1[0], surf->vertex1[1], surf->vertex1[2],
        surf->vertex2[0], surf->vertex2[1], surf->vertex2[2],
        surf->vertex3[0], surf->vertex3[1], surf->vertex3[2]
    );
    CONS_Printf(
        "%s verts(srb2): (%.2f, %.2f, %.2f) (%.2f, %.2f, %.2f) (%.2f, %.2f, %.2f)\n",
        label,
        SM642DoomFloatX((float)surf->vertex1[0]), SM642DoomFloatY((float)surf->vertex1[2]), SM642DoomFloatZ((float)surf->vertex1[1]),
        SM642DoomFloatX((float)surf->vertex2[0]), SM642DoomFloatY((float)surf->vertex2[2]), SM642DoomFloatZ((float)surf->vertex2[1]),
        SM642DoomFloatX((float)surf->vertex3[0]), SM642DoomFloatY((float)surf->vertex3[2]), SM642DoomFloatZ((float)surf->vertex3[1])
    );

    surface_index = P_SM64_DebugFindSurfaceIndex(surf);
    if (surface_index >= 0)
    {
        sm64_surface_debug_info_t *info = &surface_debug_infos[surface_index];
        sector_t *current_sector = NULL;
        sector_t *other_sector = NULL;
        ffloor_t *rover;
        INT32 ffloor_count;

        CONS_Printf(
            "%s source: surface=%d kind=%s subsector=%d seg=%d current_sector=%d other_sector=%d fofflags=0x%08x\n",
            label,
            surface_index,
            P_SM64_DebugSurfaceKindName(info->kind),
            info->subsector,
            info->seg,
            info->current_sector,
            info->other_sector,
            (unsigned)info->fofflags
        );

        if (info->current_sector >= 0 && (size_t)info->current_sector < numsectors)
            current_sector = &sectors[info->current_sector];
        if (info->other_sector >= 0 && (size_t)info->other_sector < numsectors)
            other_sector = &sectors[info->other_sector];

        if (current_sector)
        {
            ffloor_count = 0;
            for (rover = current_sector->ffloors; rover; rover = rover->next)
                ffloor_count++;

            CONS_Printf(
                "%s source current-sector: idx=%d floor=%.2f ceil=%.2f floorpic=%d ceilingpic=%d skyceil=%d heightsec=%d camsec=%d ffloors=%d\n",
                label,
                info->current_sector,
                FIXED_TO_FLOAT(current_sector->floorheight),
                FIXED_TO_FLOAT(current_sector->ceilingheight),
                current_sector->floorpic,
                current_sector->ceilingpic,
                current_sector->ceilingpic == skyflatnum,
                current_sector->heightsec,
                current_sector->camsec,
                ffloor_count
            );
        }

        if (other_sector)
        {
            ffloor_count = 0;
            for (rover = other_sector->ffloors; rover; rover = rover->next)
                ffloor_count++;

            CONS_Printf(
                "%s source other-sector: idx=%d floor=%.2f ceil=%.2f floorpic=%d ceilingpic=%d skyceil=%d heightsec=%d camsec=%d ffloors=%d\n",
                label,
                info->other_sector,
                FIXED_TO_FLOAT(other_sector->floorheight),
                FIXED_TO_FLOAT(other_sector->ceilingheight),
                other_sector->floorpic,
                other_sector->ceilingpic,
                other_sector->ceilingpic == skyflatnum,
                other_sector->heightsec,
                other_sector->camsec,
                ffloor_count
            );
        }
    }
    else
    {
        CONS_Printf("%s source: <not found in exported surface list>\n", label);
    }
}

static void P_SM64_DebugProbeWallSet(const char *label, float x, float y, float z, float offsetY, float radius)
{
    struct SM64WallCollisionData probe = {0};
    INT32 hits;
    INT32 i;

    probe.x = x;
    probe.y = y;
    probe.z = z;
    probe.offsetY = offsetY;
    probe.radius = radius;

    hits = sm64_surface_find_wall_collisions(&probe);

    CONS_Printf(
        "SM64 PROBE %s: hits=%d numWalls=%d delta=(%.2f, %.2f, %.2f) offsetY=%.2f radius=%.2f\n",
        label,
        hits,
        probe.numWalls,
        probe.x - x,
        probe.y - y,
        probe.z - z,
        offsetY,
        radius
    );

    for (i = 0; i < probe.numWalls && i < 4; i++)
    {
        char wall_label[64];
        snprintf(wall_label, sizeof (wall_label), "SM64 PROBE %s wall[%d]", label, i);
        P_SM64_DebugPrintCollisionSurface(wall_label, probe.walls[i]);
    }
}

static void P_SM64_DebugProbePoint(const char *label, float x, float y, float z)
{
    struct SM64FloorCollisionData *floorGeo = NULL;
    struct SM64SurfaceCollisionData *floorSurf = NULL;
    struct SM64SurfaceCollisionData *ceilSurf = NULL;
    float floorHeight = sm64_surface_find_floor(x, y, z, &floorSurf);
    float floorHeightData = sm64_surface_find_floor_height_and_data(x, y, z, &floorGeo);
    float ceilHeight = sm64_surface_find_ceil(x, y, z, &ceilSurf);
    char floor_label[64];
    char ceil_label[64];

    CONS_Printf(
        "SM64 PROBE %s: sm64=(%.2f, %.2f, %.2f) srb2=(%.2f, %.2f, %.2f)\n",
        label,
        x, y, z,
        SM642DoomFloatX(x),
        SM642DoomFloatY(z),
        SM642DoomFloatZ(y)
    );

    if (floorGeo)
    {
        CONS_Printf(
            "SM64 PROBE %s floorGeo: height=%.2f normal=(%.3f, %.3f, %.3f) origin=%.3f distToFloor=%.2f headroom=%.2f\n",
            label,
            floorHeightData,
            floorGeo->normalX,
            floorGeo->normalY,
            floorGeo->normalZ,
            floorGeo->originOffset,
            y - floorHeightData,
            ceilHeight - floorHeightData
        );
    }
    else
    {
        CONS_Printf(
            "SM64 PROBE %s floorGeo: <none> floor=%.2f floorData=%.2f ceil=%.2f\n",
            label,
            floorHeight,
            floorHeightData,
            ceilHeight
        );
    }

    snprintf(floor_label, sizeof (floor_label), "SM64 PROBE %s floor", label);
    snprintf(ceil_label, sizeof (ceil_label), "SM64 PROBE %s ceil", label);
    P_SM64_DebugPrintCollisionSurface(floor_label, floorSurf);
    P_SM64_DebugPrintCollisionSurface(ceil_label, ceilSurf);
}

// ==========================================================================
// SM64 AUDIO STREAMING SYSTEM
// ==========================================================================
// Opens a dedicated SDL audio device at 32000 Hz stereo (SM64's native rate)
// and uses SDL_QueueAudio in push-mode -- no resampling, no gaps, matches the
// reference implementation in libsm64/test/audio.cpp exactly.

#include <SDL.h>

// sm64_audio_tick always outputs either 544 or 528 sample-frames per internal
// pass, and it runs 2 internal passes per call.  Buffer must hold the maximum:
//   544 frames * 2 passes * 2 channels = 2176 int16_t values
#define SM64_AUDIO_TICK_MAX 544
#define SM64_AUDIO_BUF_SIZE (SM64_AUDIO_TICK_MAX * 2 * 2)

static int16_t sm64_audio_buffer[SM64_AUDIO_BUF_SIZE];
static SDL_AudioDeviceID sm64_audio_dev = 0;
static boolean sm64_audio_inited = false;

// Max queued audio before we stop pushing (keeps latency low: ~187 ms)
#define SM64_AUDIO_MAX_QUEUED_FRAMES 6000

static void P_SM64_InitAudioChannel(void)
{
    SDL_AudioSpec want, have;

    if (sm64_audio_inited)
        return;

    SDL_zero(want);
    want.freq     = 32000;      // SM64 native rate -- no resampling needed
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 512;
    want.callback = NULL;       // push-mode: we call SDL_QueueAudio ourselves

    sm64_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (sm64_audio_dev == 0)
    {
        CONS_Printf("SM64 AUDIO: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return;
    }

    SDL_PauseAudioDevice(sm64_audio_dev, 0); // start playback immediately
    sm64_audio_inited = true;

    CONS_Printf("SM64 AUDIO: device opened at %d Hz %dch (push-mode)\n",
                have.freq, have.channels);
}

static void P_SM64_DestroyAudio(void)
{
    if (!sm64_audio_inited)
        return;
    SDL_CloseAudioDevice(sm64_audio_dev);
    sm64_audio_dev  = 0;
    sm64_audio_inited = false;
}

static void P_SM64_GenerateAudio(void)
{
    uint32_t queued_bytes;
    uint32_t queued_frames;
    uint32_t num_samples;

    if (!sm64_initialized || !sm64_audio_inited)
        return;

    // How many stereo frames are already sitting in SDL's output queue?
    // Each frame = 2 ch * sizeof(int16_t) = 4 bytes.
    queued_bytes  = SDL_GetQueuedAudioSize(sm64_audio_dev);
    queued_frames = queued_bytes / 4;

    // Don't over-buffer -- avoid runaway latency buildup.
    if (queued_frames >= SM64_AUDIO_MAX_QUEUED_FRAMES)
        return;

    // sm64_audio_tick:
    //   arg1 (numQueuedSamples)  -- tells SM64 how full our buffer is so it
    //                               can pick SAMPLES_HIGH (544) vs SAMPLES_LOW
    //                               (528) to stay in sync with real-time.
    //   arg2 (numDesiredSamples) -- SM64 ignores this; 1100 is the reference
    //                               value used by the official test program.
    //   return value             -- frames per internal pass (528 or 544).
    //                               SM64 runs 2 passes, so total output is
    //                               retval * 2 stereo frames.
    num_samples = sm64_audio_tick(queued_frames, 1100, sm64_audio_buffer);

    if (num_samples == 0)
        return;

    // Queue all generated frames: num_samples passes * 2 internal ticks
    // * 4 bytes/frame (stereo int16_t).
    SDL_QueueAudio(sm64_audio_dev, sm64_audio_buffer, num_samples * 2 * 4);
}

void P_SM64_CreateMario(player_t *player)
{
    if (!sm64_initialized || !player->mo) return;
    mario_triangle_counts[P_SM64_PlayerIndex(player)] = 0;

    // Initialize audio channel for Mario sounds
#if defined(HAVE_MIXER) || defined(HAVE_MIXERX)
    P_SM64_InitAudioChannel();
#endif

    // Load current surfaces (if not loaded already)
    P_SM64_LoadSurfaces();

    // Initialize Mario at player's start pos
    float mx = Doom2SM64X(player->mo->x);
    float my = Doom2SM64Y(player->mo->z) + 10.0f; // Spawn slightly above floor
    float mz = Doom2SM64Z(player->mo->y);
    
    int32_t marioId = sm64_mario_create(mx, my, mz);
    
    if (marioId == -1) {
        CONS_Printf("SM64: Failed to create Mario! (No floor under %.2f, %.2f, %.2f?)\n", mx, my, mz);
        player->sm64_active = false;
        return;
    }
    
    CONS_Printf("SM64: Mario created successfully with ID %d\n", marioId);
    player->sm64_active = true;
    player->sm64_mario = (void*)(intptr_t)marioId;
}

void P_SM64_RemoveMario(player_t *player)
{
    if (!sm64_initialized || !player->sm64_active) return;
    sm64_mario_delete((int32_t)(intptr_t)player->sm64_mario);
    player->sm64_active = false;
    mario_triangle_counts[P_SM64_PlayerIndex(player)] = 0;
}

// SM64 action flags usados para interação
#define SM64_ACT_FLAG_ATTACKING (1 << 23) // 0x00800000
#define SM64_ACT_FREEFALL       0x0100088C

// ---------------------------------------------------------------------------
// P_SM64_ObjectInteraction
// Detecta e aplica interações com objetos SRB2 (molas, itens, inimigos).
// Chamado em P_SM64_Tick após sincronização de posição.
// ---------------------------------------------------------------------------
static void P_SM64_ObjectInteraction(player_t *player, int32_t marioId, const struct SM64MarioState *state)
{
    mobj_t *mo = player->mo;
    bthingit_t *it;
    mobj_t *thing;
    fixed_t interact_range;
    int bx1, by1, bx2, by2;
    boolean sprung = false;
    fixed_t spring_launch_x = 0, spring_launch_y = 0, spring_launch_z = 0;
    float spring_vx = 0.0f, spring_vy = 0.0f, spring_vz = 0.0f;

    if (!mo) return;

    // MF_NOCLIPTHING impede que PIT_CheckThing processe colisões de badniks
    // contra o mobj do Mario (p_map.c:868). Sem isso, quando um badnik se move
    // via P_TryMove e toca Mario, o SRB2 dispara P_TouchSpecialThing(badnik, mario)
    // que chama P_DamageMobj/P_KillMobj no player, corrompendo seu estado
    // permanentemente. Nossa função cuida de tudo manualmente.
    mo->flags |= MF_NOCLIPTHING;

    interact_range = mo->radius + 64*FRACUNIT;

    bx1 = (mo->x - interact_range - bmaporgx) >> MAPBLOCKSHIFT;
    by1 = (mo->y - interact_range - bmaporgy) >> MAPBLOCKSHIFT;
    bx2 = (mo->x + interact_range - bmaporgx) >> MAPBLOCKSHIFT;
    by2 = (mo->y + interact_range - bmaporgy) >> MAPBLOCKSHIFT;

    it = P_NewBlockThingsIterator(bx1, by1, bx2, by2);
    if (!it) return;

    while ((thing = P_BlockThingsIteratorNext(it, false)) != NULL)
    {
        fixed_t dx, dy, combined_radius;
        boolean is_enemy;

        if (thing == mo) continue;
        if (!thing->health) continue;
        if (P_MobjWasRemoved(thing)) continue;

        // Rejeição retangular rápida em XY
        combined_radius = mo->radius + thing->radius;
        dx = mo->x - thing->x;
        dy = mo->y - thing->y;
        if (dx < -combined_radius || dx > combined_radius) continue;
        if (dy < -combined_radius || dy > combined_radius) continue;

        // Sobreposição vertical.
        // Molas: SM64 não sabe que elas existem como superfície, então Mario
        // pode cair ATRAVÉS delas em um tick. Tolerância generosa para cima
        // (128 unidades) captura Mario em queda rápida antes de passar.
        if (mo->z > thing->z + thing->height + 8*FRACUNIT) continue;
        if (mo->z + mo->height < thing->z - 24*FRACUNIT) continue;

        is_enemy = (thing->flags & (MF_ENEMY|MF_BOSS)) && !(thing->flags & MF_MISSILE);

        // ---- Molas ---------------------------------------------------------
        if (thing->flags & MF_SPRING)
        {
            if (P_DoSpring(thing, mo))
            {
                // P_DoSpring ajusta mo->z para cima da mola — salva para sincronizar
                spring_launch_x = mo->x;
                spring_launch_y = mo->y;
                spring_launch_z = mo->z;

                // Converte momento SRB2 → velocidade SM64
                spring_vx = FIXED_TO_FLOAT(mo->momx) * SM64_SCALE;
                spring_vy = FIXED_TO_FLOAT(mo->momz) * SM64_SCALE;
                spring_vz = -FIXED_TO_FLOAT(mo->momy) * SM64_SCALE;
                sprung = true;

                mo->momx = 0;
                mo->momy = 0;
                mo->momz = 0;
            }
            continue;
        }

        // ---- Monitores / caixas --------------------------------------------
        // Flags: MF_SOLID|MF_SHOOTABLE|MF_MONITOR (sem MF_SPECIAL nem MF_ENEMY)
        if (thing->flags & MF_MONITOR)
        {
            float ex       = Doom2SM64X(thing->x);
            float ey       = Doom2SM64Y(thing->z);
            float ez       = Doom2SM64Z(thing->y);
            float hitbox_h = FIXED_TO_FLOAT(thing->height) * SM64_SCALE;

            if ((state->action & SM64_ACT_FLAG_ATTACKING) ||
                sm64_mario_attack(marioId, ex, ey, ez, hitbox_h))
                P_DamageMobj(thing, mo, mo, 1, 0);
            continue;
        }

        // ---- Inimigos e chefes ---------------------------------------------
        if (is_enemy)
        {
            float ex       = Doom2SM64X(thing->x);
            float ey       = Doom2SM64Y(thing->z);
            float ez       = Doom2SM64Z(thing->y);
            float hitbox_h = FIXED_TO_FLOAT(thing->height) * SM64_SCALE;

            if (sm64_mario_attack(marioId, ex, ey, ez, hitbox_h))
            {
                P_DamageMobj(thing, mo, mo, 1, 0);
            }
            else if (!player->powers[pw_flashing] && !player->powers[pw_invulnerability])
            {
                // Dano de contato: usa o sistema interno do SM64 (knockback + animação)
                // e aplica perda de anéis manualmente, sem tocar no pipeline de dano SRB2.
                sm64_mario_take_damage(marioId, 1, 0,
                    Doom2SM64X(thing->x),
                    Doom2SM64Y(thing->z + thing->height/2),
                    Doom2SM64Z(thing->y));

                if (player->rings > 0)
                {
                    P_PlayerRingBurst(player, player->rings);
                    player->rings = 0;
                }
                player->powers[pw_flashing] = flashingtics;
            }
            continue;
        }

        // ---- Coletáveis / pickups (anéis, escudos, etc.) -------------------
        // Só chegamos aqui para MF_SPECIAL que NÃO são inimigos nem monitores.
        if (thing->flags & MF_SPECIAL)
        {
            P_TouchSpecialThing(thing, mo, false);
            continue;
        }
    }

    P_FreeBlockThingsIterator(it);

    // Aplica lançamento de mola — posição ANTES da velocidade é fundamental:
    // sm64_set_mario_position teleporta Mario para o topo da mola, depois a
    // velocidade leva embora. Sem isso a gravidade cancela o impulso imediatamente.
    if (sprung)
    {
        sm64_set_mario_position(marioId,
            Doom2SM64X(spring_launch_x),
            Doom2SM64Y(spring_launch_z),
            Doom2SM64Z(spring_launch_y));
        sm64_set_mario_velocity(marioId, spring_vx, spring_vy, spring_vz);
        sm64_set_mario_action(marioId, SM64_ACT_FREEFALL);
    }
}

void P_SM64_Tick(player_t *player)
{
    if (!sm64_initialized || !player->mo || !player->sm64_active) return;

    struct SM64MarioInputs inputs = {0};
    size_t player_index = P_SM64_PlayerIndex(player);
    angle_t view_angle;
    
    // libsm64 flips stickX internally, but not stickY.
    inputs.stickX = (float)player->cmd.sidemove / 50.0f;
    inputs.stickY = -(float)player->cmd.forwardmove / 50.0f;
    
    view_angle = player->mo->angle;
    if (player == &players[consoleplayer] || player == &players[secondarydisplayplayer])
        view_angle = P_GetLocalAngle(player);

    {
        float angle = (float)view_angle / (float)(0xFFFFFFFF) * 3.1415926535f * 2.0f;
    
        // SRB2's Y axis points the opposite way from SM64's Z axis.
        inputs.camLookX = cosf(angle);
        inputs.camLookZ = -sinf(angle);
    }
    
    if (player->cmd.buttons & BT_JUMP) inputs.buttonA = 1;
    if (player->cmd.buttons & BT_SPIN) inputs.buttonZ = 1;
    if (player->cmd.buttons & BT_ATTACK) inputs.buttonB = 1;

    struct SM64MarioState state = {0};
    struct SM64MarioGeometryBuffers buffers;
    buffers.position = mario_pos_buffers[player_index];
    buffers.normal = mario_normal_buffers[player_index];
    buffers.color = mario_color_buffers[player_index];
    buffers.uv = mario_uv_buffers[player_index];
    buffers.numTrianglesUsed = 0;

    int32_t marioId = (int32_t)(intptr_t)player->sm64_mario;

    sm64_mario_tick(marioId, &inputs, &state, &buffers);

    // Generate SM64 audio (only for consoleplayer to avoid duplicate sounds)
#if defined(HAVE_MIXER) || defined(HAVE_MIXERX)
    if (player == &players[consoleplayer])
        P_SM64_GenerateAudio();
#endif

    // SM64.X = SRB2.X
    // SM64.Y = SRB2.Z (Vertical)
    // SM64.Z = -SRB2.Y

    mario_triangle_counts[player_index] = buffers.numTrianglesUsed;

    fixed_t nx = SM642DoomX(state.position[0]);
    fixed_t ny = SM642DoomY(state.position[2]);
    fixed_t nz = SM642DoomZ(state.position[1]);

    // Update position directly without P_TeleportMove to avoid SRB2 collision interference
    // SM64 handles all collision internally, we just sync the position back
    player->mo->x = nx;
    player->mo->y = ny;
    player->mo->z = nz;

    // Unlink and relink to maintain sector/subsector consistency
    P_UnsetThingPosition(player->mo);
    P_SetThingPosition(player->mo);

    // Reset momentum - SM64 controls all movement
    player->mo->momx = 0;
    player->mo->momy = 0;
    player->mo->momz = 0;

    // Object interaction: springs, collectibles, enemies
    P_SM64_ObjectInteraction(player, marioId, &state);

    // Decrementa pw_flashing manualmente — P_PlayerThink é bypassado para SM64
    // então o decremento normal em p_user.c:12378 nunca roda.
    if (player->powers[pw_flashing] && player->powers[pw_flashing] < UINT16_MAX)
        player->powers[pw_flashing]--;

    if (leveltime % (TICRATE*2) == 0) {
        CONS_Printf("Mario ID %d | POS: %.2f, %.2f, %.2f | Act: %x\n", marioId, state.position[0], state.position[1], state.position[2], state.action);
    }
}

UINT32 P_SM64_GetTriangleCount(const player_t *player)
{
    return mario_triangle_counts[P_SM64_PlayerIndex(player)];
}

const float *P_SM64_GetPositionBuffer(const player_t *player)
{
    return mario_pos_buffers[P_SM64_PlayerIndex(player)];
}

const float *P_SM64_GetColorBuffer(const player_t *player)
{
    return mario_color_buffers[P_SM64_PlayerIndex(player)];
}

const float *P_SM64_GetUVBuffer(const player_t *player)
{
    return mario_uv_buffers[P_SM64_PlayerIndex(player)];
}

const UINT8 *P_SM64_GetTextureData(void)
{
    return texture_buffer;
}

INT32 P_SM64_GetTextureWidth(void)
{
    return SM64_TEXTURE_WIDTH;
}

INT32 P_SM64_GetTextureHeight(void)
{
    return SM64_TEXTURE_HEIGHT;
}

void P_SM64_SetNextSurfaceDebugInfo(sm64_surface_debug_kind_t kind, INT32 subsector, INT32 seg,
	INT32 current_sector, INT32 other_sector, UINT32 fofflags)
{
	next_surface_debug_info.kind = kind;
	next_surface_debug_info.subsector = subsector;
	next_surface_debug_info.seg = seg;
	next_surface_debug_info.current_sector = current_sector;
	next_surface_debug_info.other_sector = other_sector;
	next_surface_debug_info.fofflags = fofflags;
}

void P_SM64_DebugProbeCollision(player_t *player)
{
    angle_t view_angle;
    float angle;
    float sm64_x;
    float sm64_y;
    float sm64_z;
    float ahead_x;
    float ahead_z;
    const float ahead_distance = 16.0f * SM64_SCALE;

    if (!sm64_initialized)
    {
        CONS_Printf("SM64 PROBE: libsm64 is not initialized\n");
        return;
    }

    if (!player || !player->mo)
    {
        CONS_Printf("SM64 PROBE: player/mo is missing\n");
        return;
    }

    sm64_x = Doom2SM64X(player->mo->x);
    sm64_y = Doom2SM64Y(player->mo->z);
    sm64_z = Doom2SM64Z(player->mo->y);

    view_angle = player->mo->angle;
    if (player == &players[consoleplayer] || player == &players[secondarydisplayplayer])
        view_angle = P_GetLocalAngle(player);

    angle = (float)view_angle / (float)(0xFFFFFFFF) * 3.1415926535f * 2.0f;
    ahead_x = sm64_x + cosf(angle) * ahead_distance;
    ahead_z = sm64_z - sinf(angle) * ahead_distance;

    CONS_Printf(
        "SM64 PROBE begin: active=%d mario=%p angle=0x%08x surfaces=%u\n",
        player->sm64_active,
        player->sm64_mario,
        (unsigned)view_angle,
        num_surfaces
    );

    P_SM64_DebugProbePoint("current", sm64_x, sm64_y, sm64_z);
    P_SM64_DebugProbeWallSet("current lower", sm64_x, sm64_y, sm64_z, 30.0f, 24.0f);
    P_SM64_DebugProbeWallSet("current upper", sm64_x, sm64_y, sm64_z, 60.0f, 50.0f);

    P_SM64_DebugProbePoint("ahead16", ahead_x, sm64_y, ahead_z);
    P_SM64_DebugProbeWallSet("ahead16 lower", ahead_x, sm64_y, ahead_z, 30.0f, 24.0f);
    P_SM64_DebugProbeWallSet("ahead16 upper", ahead_x, sm64_y, ahead_z, 60.0f, 50.0f);

    CONS_Printf("SM64 PROBE end\n");
}
