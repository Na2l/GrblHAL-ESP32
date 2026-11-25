/*
  heightmap.c - probed surface grid for Z compensation

  Pure module, no core/hardware dependencies: safe to unit test on a host.
  See heightmap.h for the model and the coordinate conventions.
*/

#include "heightmap.h"

#include <math.h>
#include <string.h>

#define HEIGHTMAP_EPS 1e-4f

static heightmap_t heightmap;

static bool is_finite_f (float v)
{
    return isfinite(v) != 0;
}

void heightmap_init (void)
{
    memset(&heightmap, 0, sizeof(heightmap));
    heightmap.state = Heightmap_Empty;
}

heightmap_state_t heightmap_get_state (void)
{
    return heightmap.state;
}

const char *heightmap_state_name (heightmap_state_t state)
{
    switch(state) {

        case Heightmap_Ready:
            return "ready";

        case Heightmap_Probing:
            return "probing";

        case Heightmap_Invalid:
            return "invalid";

        default:
            return "empty";
    }
}

bool heightmap_begin (uint8_t n, float x0, float y0, float dx, float dy)
{
    if(heightmap.state == Heightmap_Probing)
        return false;

    if(n < 2 || n > HEIGHTMAP_MAX_GRID)
        return false;

    if(!is_finite_f(x0) || !is_finite_f(y0) || !is_finite_f(dx) || !is_finite_f(dy))
        return false;

    if(dx <= HEIGHTMAP_EPS || dy <= HEIGHTMAP_EPS)
        return false;

    memset(&heightmap, 0, sizeof(heightmap));

    heightmap.n = n;
    heightmap.x0 = x0;
    heightmap.y0 = y0;
    heightmap.dx = dx;
    heightmap.dy = dy;
    heightmap.state = Heightmap_Probing;

    return true;
}

bool heightmap_add_point (uint8_t ix, uint8_t iy, float machine_z)
{
    if(heightmap.state != Heightmap_Probing)
        return false;

    if(ix >= heightmap.n || iy >= heightmap.n)
        return false;

    if(!is_finite_f(machine_z))
        return false;

    heightmap.machine_z[ix][iy] = machine_z;
    heightmap.count++;

    return true;
}

bool heightmap_complete (void)
{
    uint8_t ix, iy;

    if(heightmap.state != Heightmap_Probing)
        return false;

    if(heightmap.count != (uint8_t)(heightmap.n * heightmap.n)) {
        heightmap.state = Heightmap_Invalid;
        return false;
    }

    for(ix = 0; ix < heightmap.n; ix++)
        for(iy = 0; iy < heightmap.n; iy++)
            if(!is_finite_f(heightmap.machine_z[ix][iy])) {
                heightmap.state = Heightmap_Invalid;
                return false;
            }

    heightmap.state = Heightmap_Ready;

    return true;
}

void heightmap_reset (void)
{
    memset(&heightmap, 0, sizeof(heightmap));
    heightmap.state = Heightmap_Empty;
}

const heightmap_t *heightmap_get (void)
{
    return heightmap.state == Heightmap_Ready ? &heightmap : NULL;
}

bool heightmap_surface_z (float x, float y, float *machine_z)
{
    float fx, fy, z00, z10, z01, z11, dzx, dzy;
    uint8_t ix, iy;

    if(machine_z == NULL || heightmap.state != Heightmap_Ready)
        return false;

    if(!is_finite_f(x) || !is_finite_f(y))
        return false;

    // Clamp to the probed area (edge hold).
    if(x < heightmap.x0)
        x = heightmap.x0;
    else if(x > heightmap.x0 + heightmap.dx * (heightmap.n - 1))
        x = heightmap.x0 + heightmap.dx * (heightmap.n - 1);

    if(y < heightmap.y0)
        y = heightmap.y0;
    else if(y > heightmap.y0 + heightmap.dy * (heightmap.n - 1))
        y = heightmap.y0 + heightmap.dy * (heightmap.n - 1);

    fx = (x - heightmap.x0) / heightmap.dx;
    fy = (y - heightmap.y0) / heightmap.dy;

    ix = (uint8_t)floorf(fx);
    iy = (uint8_t)floorf(fy);

    if(ix > heightmap.n - 2)
        ix = heightmap.n - 2;

    if(iy > heightmap.n - 2)
        iy = heightmap.n - 2;

    dzx = fx - (float)ix;
    dzy = fy - (float)iy;

    z00 = heightmap.machine_z[ix][iy];
    z10 = heightmap.machine_z[ix + 1][iy];
    z01 = heightmap.machine_z[ix][iy + 1];
    z11 = heightmap.machine_z[ix + 1][iy + 1];

    if(!is_finite_f(z00) || !is_finite_f(z10) || !is_finite_f(z01) || !is_finite_f(z11))
        return false;

    *machine_z = z00 * (1.0f - dzx) * (1.0f - dzy) +
                  z10 * dzx * (1.0f - dzy) +
                  z01 * (1.0f - dzx) * dzy +
                  z11 * dzx * dzy;

    return is_finite_f(*machine_z);
}

bool heightmap_offset (float x, float y, float wco_z, float *offset_mm)
{
    float machine_z;

    if(offset_mm == NULL || !is_finite_f(wco_z))
        return false;

    if(!heightmap_surface_z(x, y, &machine_z))
        return false;

    // Work position = machine position - work coordinate offset, so the
    // surface height relative to the current work Z=0 is simply:
    *offset_mm = machine_z - wco_z;

    return is_finite_f(*offset_mm);
}

uint32_t heightmap_checksum (void)
{
    uint32_t hash = 2166136261u;
    const uint8_t *data = (const uint8_t *)&heightmap.machine_z;
    size_t len = (size_t)heightmap.n * (size_t)heightmap.n * sizeof(float);
    size_t idx;

    for(idx = 0; idx < len; idx++) {
        hash ^= data[idx];
        hash *= 16777619u;
    }

    return hash;
}
