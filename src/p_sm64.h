// SM64 Integration for SRB2
// Based on libsm64 and gzdoom-sm64

#ifndef __P_SM64_H__
#define __P_SM64_H__

#include "doomdef.h"
#include "d_player.h"

typedef enum
{
	SM64_SURFACE_DEBUG_NONE = 0,
	SM64_SURFACE_DEBUG_SECTOR_FLOOR,
	SM64_SURFACE_DEBUG_SECTOR_CEILING,
	SM64_SURFACE_DEBUG_FOF_TOP,
	SM64_SURFACE_DEBUG_FOF_BOTTOM,
	SM64_SURFACE_DEBUG_WALL_ONE_SIDED,
	SM64_SURFACE_DEBUG_WALL_FLOOR_STEP,
	SM64_SURFACE_DEBUG_WALL_CEILING_GAP,
} sm64_surface_debug_kind_t;

// Initialize SM64 library with a ROM file
boolean P_SM64_Init(const char *rom_path);

// Shutdown SM64 library
void P_SM64_Shutdown(void);

// Reset SM64 for a new level
void P_SM64_LevelSetup(void);

// Create a Mario instance for a player
void P_SM64_CreateMario(player_t *player);

// Remove a Mario instance
void P_SM64_RemoveMario(player_t *player);

// Tick Mario physics
void P_SM64_Tick(player_t *player);

// Access the current frame mesh for hardware rendering.
UINT32 P_SM64_GetTriangleCount(const player_t *player);
const float *P_SM64_GetPositionBuffer(const player_t *player);
const float *P_SM64_GetColorBuffer(const player_t *player);
const float *P_SM64_GetUVBuffer(const player_t *player);
const UINT8 *P_SM64_GetTextureData(void);
INT32 P_SM64_GetTextureWidth(void);
INT32 P_SM64_GetTextureHeight(void);

// Tags subsequent exported surfaces with source info for debug probes.
void P_SM64_SetNextSurfaceDebugInfo(sm64_surface_debug_kind_t kind, INT32 subsector, INT32 seg,
	INT32 current_sector, INT32 other_sector, UINT32 fofflags);

// Print libsm64 floor/wall probe data for the player's current position.
void P_SM64_DebugProbeCollision(player_t *player);

// Convert SRB2 geometry to SM64 surfaces
// This will be called from p_setup.c or hw_bsp.c
void P_SM64_AddStaticSurface(float x1, float y1, float z1,
                             float x2, float y2, float z2,
                             float x3, float y3, float z3,
                             int terrain_type, boolean is_ceiling);

#endif
