/*
  heightmap.h - probed surface grid for Z compensation

  Part of grblHAL ESP32 driver (MKS DLC32 MAX / ESP32-S3)

  Purpose: store a probed surface grid in machine coordinates, so the
  compensation offset for any XY position can be computed as

      offset_work_z(x, y) = surface_machine_z(x, y) - wco_z_now

  which is the true height of the surface relative to the current work
  Z=0 plane.  Storing machine Z (instead of offsets relative to the first
  probe) keeps the map valid if the work zero is changed after probing.

  The module is pure (no hardware/core dependencies) and fail-closed:
  interpolation is only allowed when the map is complete and every value
  is finite.
*/

#ifndef __HEIGHTMAP_H__
#define __HEIGHTMAP_H__

#include <stdbool.h>
#include <stdint.h>

#ifndef HEIGHTMAP_ENABLE
#define HEIGHTMAP_ENABLE 0
#endif

/// Maximum grid size (N x N probed points).
#define HEIGHTMAP_MAX_GRID 9

typedef enum {
    Heightmap_Empty = 0,   //!< No map (never probed or cleared).
    Heightmap_Probing,     //!< Probing in progress (map incomplete).
    Heightmap_Ready,       //!< Complete map, usable for compensation.
    Heightmap_Invalid      //!< Probing failed/aborted; must be re-probed.
} heightmap_state_t;

typedef struct {
    heightmap_state_t state;
    uint8_t n;                                                       //!< Grid size (points per axis).
    uint8_t count;                                                   //!< Number of stored points.
    float x0, y0;                                                    //!< Work XY of grid origin (point [0][0]).
    float dx, dy;                                                    //!< Work XY spacing between points.
    float machine_z[HEIGHTMAP_MAX_GRID][HEIGHTMAP_MAX_GRID];         //!< Probed surface Z, machine coords (mm).
} heightmap_t;

/// Initialize the heightmap data module (called from the plugin init).
void heightmap_init (void);

/// Register settings/M-codes; called from the driver init.
void heightmap_plugin_init (void);

heightmap_state_t heightmap_get_state (void);
const char *heightmap_state_name (heightmap_state_t state);

/// Start a new map. Returns false if a probe is already running or parameters
/// are out of range. Any previous map is invalidated.
bool heightmap_begin (uint8_t n, float x0, float y0, float dx, float dy);

/// Store one probed point. ix/iy are grid indices, machine_z is the machine
/// Z coordinate of the surface at that point (mm). Rejects duplicates/out of
/// range values.
bool heightmap_add_point (uint8_t ix, uint8_t iy, float machine_z);

/// Finish probing. Returns true only if all N*N points are present and finite.
bool heightmap_complete (void);

/// Invalidate/clear the map.
void heightmap_reset (void);

/// Bilinear interpolation of the probed surface (machine Z) at work XY.
/// Returns false unless the map is complete and the value is finite. XY
/// outside the grid is clamped to the closest edge point.
bool heightmap_surface_z (float x, float y, float *machine_z);

/// Compensation offset at work XY: surface height relative to the current
/// work Z=0 plane (pass wco_z = current Z work coordinate offset).
bool heightmap_offset (float x, float y, float wco_z, float *offset_mm);

const heightmap_t *heightmap_get (void);

/// Checksum over the stored grid (for persistence/validity checks later).
uint32_t heightmap_checksum (void);

#endif // __HEIGHTMAP_H__
