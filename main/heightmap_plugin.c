/*
  heightmap_plugin.c - heightmap probing plugin (grblHAL ESP32 driver)

  M-codes (private/user range, see gcode.h):
     M102            probe the surface grid (blocking, like a canned cycle)
     M103            report heightmap state / probed values
     M104            clear (invalidate) the heightmap

  Probing runs in the same context as a G38.2 cycle (parser/protocol), using
  the core motion + probe code.  The map stores machine Z per grid point, so
  the compensation offset is derived at run time as surface_machine_z - wco_z.
  Fail-closed: if any probe fails the whole map is invalidated and the core
  leaves the machine in ALARM (as with a normal failed G38.2).

  Compensation while streaming G-code is the next increment and is NOT part
  of this file yet (heightmap_offset() is the entry point for it).
*/

#include "driver.h"

#if HEIGHTMAP_ENABLE

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "grbl/gcode.h"
#include "grbl/hal.h"
#include "grbl/messages.h"
#include "grbl/motion_control.h"
#include "grbl/planner.h"
#include "grbl/protocol.h"
#include "grbl/report.h"
#include "grbl/settings.h"
#include "grbl/state_machine.h"
#include "grbl/system.h"

#include "heightmap.h"

#ifndef HEIGHTMAP_GRID_N
#define HEIGHTMAP_GRID_N 5                 // points per axis
#endif
#ifndef HEIGHTMAP_PROBE_SIZE_MM
#define HEIGHTMAP_PROBE_SIZE_MM 60.0f      // probed square size (mm)
#endif
#ifndef HEIGHTMAP_PROBE_FEED_MMPM
#define HEIGHTMAP_PROBE_FEED_MMPM 50.0f    // probe feed (mm/min)
#endif
#ifndef HEIGHTMAP_PROBE_TRAVEL_MM
#define HEIGHTMAP_PROBE_TRAVEL_MM 25.0f    // max probe travel below start Z (mm)
#endif
#ifndef HEIGHTMAP_RETRACT_MM
#define HEIGHTMAP_RETRACT_MM 2.0f          // retract/clearance above surface (mm)
#endif

static user_mcode_ptrs_t hm_user_mcode;
static char hm_msg_buf[96];

static void hm_report_msg (const char *text, message_type_t type)
{
    strncpy(hm_msg_buf, text, sizeof(hm_msg_buf) - 1);
    hm_msg_buf[sizeof(hm_msg_buf) - 1] = '\0';
    report_message(hm_msg_buf, type);
}

// Initializes planner data for plugin moves: spindle/coolant off, feed as given.
static void hm_plan_data (plan_line_data_t *plan, float feed, bool rapid)
{
    spindle_t *spindle;

    memset(plan, 0, sizeof(*plan));

    plan->feed_rate = feed;
    plan->offset_id = gc_state.offset_id;
    plan->overrides = gc_state.modal.override_ctrl;
    plan->condition.rapid_motion = rapid;
    plan->condition.target_valid = plan->condition.target_validated = sys.soft_limits.mask == 0;

    if((spindle = gc_spindle_get(-1)) != NULL) {
        memcpy(&plan->spindle, spindle, sizeof(spindle_t));
        plan->spindle.state.on = Off;               // never cut while probing
    }
}

// Absolute move to (x, y[, z]) in work coordinates; z == NAN keeps Z.
static bool hm_move_to (float x, float y, float z, bool rapid, float feed)
{
    float target[N_AXIS];
    plan_line_data_t plan;
    status_code_t status;

    memcpy(target, gc_state.position, sizeof(target));
    target[X_AXIS] = x;
    target[Y_AXIS] = y;
    if(!isnan(z))
        target[Z_AXIS] = z;

    hm_plan_data(&plan, feed, rapid);

    if((status = mc_line(target, &plan)) != Status_OK)
        return false;

    memcpy(gc_state.position, target, sizeof(target));

    return protocol_buffer_synchronize();
}

// Probe toward -Z by HEIGHTMAP_PROBE_TRAVEL_MM from the current position.
// On success stores the machine Z of the touched surface in *machine_z.
static bool hm_probe_z (float *machine_z)
{
    float target[N_AXIS];
    plan_line_data_t plan;
    gc_parser_flags_t flags = {0};
    gc_probe_t result;

    memcpy(target, gc_state.position, sizeof(target));
    target[Z_AXIS] -= HEIGHTMAP_PROBE_TRAVEL_MM;

    hm_plan_data(&plan, HEIGHTMAP_PROBE_FEED_MMPM, false);

    result = mc_probe_cycle(target, &plan, flags);

    if(result != GCProbe_Found)
        return false;

    memcpy(gc_state.position, target, sizeof(target));
    *machine_z = (float)sys.probe_position[Z_AXIS] / settings.axis[Z_AXIS].steps_per_mm;

    return true;
}

static bool hm_preconditions_ok (void)
{
    probe_state_t probe = {0};

    if(state_get() != STATE_IDLE) {
        hm_report_msg("HM: machine not idle", Message_Warning);
        return false;
    }

    if(hal.stream.file != NULL) {
        hm_report_msg("HM: job is streaming", Message_Warning);
        return false;
    }

    if(heightmap_get_state() == Heightmap_Probing) {
        hm_report_msg("HM: probing already running", Message_Warning);
        return false;
    }

    if(hal.probe.get_state == NULL) {
        hm_report_msg("HM: no probe input", Message_Error);
        return false;
    }

    probe = hal.probe.get_state();

    if(!probe.connected) {
        hm_report_msg("HM: probe not connected", Message_Error);
        return false;
    }

    if(probe.triggered) {
        hm_report_msg("HM: probe already triggered (check wiring/$6)", Message_Error);
        return false;
    }

    return true;
}

static void hm_start_probe (void)
{
    uint8_t ix, iy;
    uint8_t n = HEIGHTMAP_GRID_N;
    float x0 = gc_state.position[X_AXIS];
    float y0 = gc_state.position[Y_AXIS];
    float step = HEIGHTMAP_PROBE_SIZE_MM / (float)(n - 1);
    float machine_z;
    const heightmap_t *hm;

    if(!hm_preconditions_ok())
        return;

    if(!heightmap_begin(n, x0, y0, step, step)) {
        hm_report_msg("HM: cannot start probing", Message_Error);
        return;
    }

    hm_report_msg("HM: probing started", Message_Info);

    // Lift to a safe height before traversing to the first point.
    if(!hm_move_to(x0, y0, gc_state.position[Z_AXIS] + HEIGHTMAP_RETRACT_MM, true, 0.0f))
        goto failed;

    for(ix = 0; ix < n; ix++) {

        for(iy = 0; iy < n; iy++) {

            if(!hm_move_to(x0 + step * (float)ix, y0 + step * (float)iy, NAN, true, 0.0f))
                goto failed;

            if(!hm_probe_z(&machine_z))
                goto failed;

            if(!heightmap_add_point(ix, iy, machine_z)) {
                hm_report_msg("HM: invalid probe value", Message_Error);
                goto failed;
            }

            snprintf(hm_msg_buf, sizeof(hm_msg_buf), "HM: point %u/%u (%.2f,%.2f) z=%.3f",
                      (unsigned)(ix * n + iy + 1), (unsigned)(n * n),
                      (double)(x0 + step * (float)ix), (double)(y0 + step * (float)iy),
                      (double)machine_z);
            report_message(hm_msg_buf, Message_Info);

            // Retract after each point so the next traverse is above the surface.
            if(!hm_move_to(gc_state.position[X_AXIS], gc_state.position[Y_AXIS],
                           gc_state.position[Z_AXIS] + HEIGHTMAP_RETRACT_MM, true, 0.0f))
                goto failed;
        }
    }

    if(!heightmap_complete()) {
        hm_report_msg("HM: incomplete map", Message_Error);
        goto failed;
    }

    hm = heightmap_get();

    snprintf(hm_msg_buf, sizeof(hm_msg_buf), "HM: ready n=%u size=%.1fmm cks=0x%08x",
              (unsigned)hm->n, (double)(hm->dx * (float)(hm->n - 1)), (unsigned)heightmap_checksum());
    report_message(hm_msg_buf, Message_Info);

    return;

failed:
    heightmap_reset();
    hm_report_msg("HM: probing failed, map cleared", Message_Error);
}

static void hm_report_status (void)
{
    const heightmap_t *hm = heightmap_get();
    float wco_z = gc_get_offset(Z_AXIS, false);
    char row[80];
    uint8_t ix, iy;
    size_t pos;

    if(hm == NULL) {
        snprintf(hm_msg_buf, sizeof(hm_msg_buf), "HM: state=%s", heightmap_state_name(heightmap_get_state()));
        report_message(hm_msg_buf, Message_Info);
        return;
    }

    snprintf(hm_msg_buf, sizeof(hm_msg_buf), "HM: state=ready n=%u size=%.1fx%.1fmm cks=0x%08x points=%u",
              (unsigned)hm->n,
              (double)(hm->dx * (float)(hm->n - 1)),
              (double)(hm->dy * (float)(hm->n - 1)),
              (unsigned)heightmap_checksum(),
              (unsigned)hm->count);
    report_message(hm_msg_buf, Message_Info);

    for(ix = 0; ix < hm->n; ix++) {

        pos = (size_t)snprintf(row, sizeof(row), "HM:%u", (unsigned)ix);

        for(iy = 0; iy < hm->n && pos < sizeof(row) - 12; iy++)
            pos += (size_t)snprintf(row + pos, sizeof(row) - pos, " %.3f",
                                     (double)(hm->machine_z[ix][iy] - wco_z));

        report_message(row, Message_Info);
    }
}

static user_mcode_type_t hm_mcode_check (user_mcode_t mcode)
{
    return mcode == UserMCode_Generic2 || mcode == UserMCode_Generic3 || mcode == UserMCode_Generic4
             ? UserMCode_NoValueWords
             : (hm_user_mcode.check ? hm_user_mcode.check(mcode) : UserMCode_Unsupported);
}

static status_code_t hm_mcode_validate (parser_block_t *gc_block)
{
    switch(gc_block->user_mcode) {

        case UserMCode_Generic2:
        case UserMCode_Generic3:
        case UserMCode_Generic4:
            return Status_OK;

        default:
            return hm_user_mcode.validate ? hm_user_mcode.validate(gc_block) : Status_Unhandled;
    }
}

static void hm_mcode_execute (sys_state_t state, parser_block_t *gc_block)
{
    switch(gc_block->user_mcode) {

        case UserMCode_Generic2:
            hm_start_probe();
            break;

        case UserMCode_Generic3:
            hm_report_status();
            break;

        case UserMCode_Generic4:
            heightmap_reset();
            hm_report_msg("HM: map cleared", Message_Info);
            break;

        default:
            if(hm_user_mcode.execute)
                hm_user_mcode.execute(state, gc_block);
            break;
    }
}

void heightmap_plugin_init (void)
{
    heightmap_init();

    memcpy(&hm_user_mcode, &grbl.user_mcode, sizeof(user_mcode_ptrs_t));

    grbl.user_mcode.check = hm_mcode_check;
    grbl.user_mcode.validate = hm_mcode_validate;
    grbl.user_mcode.execute = hm_mcode_execute;

    report_plugin("Heightmap", "0.1");
}

#endif // HEIGHTMAP_ENABLE
