/*
  ui.c - embedded web UI server (grblHAL ESP32 driver / MKS DLC32 MAX)

  Serves a self-contained single page app on port 80 plus a small JSON API.
  Replaces the stock ESP3D-WebUI page. See docs/WEBUI_DESIGN.md.

  Threading: HTTP handlers run in the esp_http_server task (core 0). They never
  touch grbl state directly: a snapshot task running in the grbl task context
  copies state/positions/pins ~10 Hz into `ui_status`. Commands are injected
  with grbl.enqueue_gcode() / protocol_enqueue_realtime_command().
*/

#include "driver.h"

#if UI_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_http_server.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "grbl/gcode.h"
#include "grbl/hal.h"
#include "grbl/machine_limits.h"
#include "grbl/probe.h"
#include "grbl/protocol.h"
#include "grbl/report.h"
#include "grbl/settings.h"
#include "grbl/spindle_control.h"
#include "grbl/state_machine.h"
#include "grbl/system.h"
#include "grbl/task.h"
#include "grbl/vfs.h"

#include "build_stamp.h"
#include "heightmap.h"
#include "ota.h"
#include "ui.h"

#include "index_html.h"

#define UI_HTTP_PORT 80
#define UI_JSON_MAX 3072

typedef struct {
    volatile uint8_t state;
    volatile float pos[N_AXIS];
    volatile uint8_t limits;
    volatile bool probe;
    volatile bool door;
    volatile bool estop;
    volatile bool spindle;
    volatile uint8_t alarm;
    volatile uint8_t last_alarm;
    volatile float wco_z;
    volatile uint32_t uptime_ms;
} ui_status_t;

static ui_status_t ui_status;
static httpd_handle_t ui_server = NULL;
static volatile uint8_t ui_limits_seen;      // limit inputs seen since last clear
static uint8_t ui_limits_prev;

// ---------------------------------------------------------------------------
// Output capture (so the UI terminal can show replies): prefix the active
// stream writer, append to a bounded ring buffer.
// ---------------------------------------------------------------------------

#define UI_RB_SIZE 8192

static char ui_rb[UI_RB_SIZE];
static volatile uint32_t ui_rb_head, ui_rb_tail;
static stream_write_ptr ui_orig_write;

static IRAM_ATTR void ui_write (const char *s)
{
    const char *p = s;

    while(*s) {
        uint32_t next = (ui_rb_head + 1) % UI_RB_SIZE;
        if(next == ui_rb_tail)
            break;                          // drop when full
        ui_rb[ui_rb_head] = *s++;
        ui_rb_head = next;
    }

    if(ui_orig_write)
        ui_orig_write(p);
}

// ---------------------------------------------------------------------------
// Snapshot (grbl task context)
// ---------------------------------------------------------------------------

static void ui_snapshot_task (void *arg)
{
    axes_signals_t lim = {0};
    control_signals_t ctl = {0};
    probe_state_t probe = {0};
    spindle_t *spindle;

    ui_status.state = (uint8_t)state_get();
    ui_status.alarm = (ui_status.state & STATE_ALARM) ? (uint8_t)sys.alarm : 0;
    if(ui_status.alarm != 0)
        ui_status.last_alarm = ui_status.alarm;
    memcpy((void *)ui_status.pos, gc_state.position, sizeof(float) * N_AXIS);

    if(hal.limits.get_state)
        lim = limit_signals_merge(hal.limits.get_state());
    if(hal.control.get_state)
        ctl = hal.control.get_state();
    if(hal.probe.get_state)
        probe = hal.probe.get_state();

    ui_status.limits = lim.mask;
    ui_limits_seen |= (uint8_t)(lim.mask & ~ui_limits_prev);
    ui_limits_prev = lim.mask;
    ui_status.probe = probe.triggered;
    ui_status.door = ctl.safety_door_ajar;
    ui_status.estop = ctl.e_stop;
    ui_status.wco_z = gc_get_offset(Z_AXIS, false);
    ui_status.uptime_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    ui_status.spindle = false;
    if((spindle = gc_spindle_get(-1)) != NULL)
        ui_status.spindle = spindle->state.on;

    // Keep the terminal capture attached if the active stream changed
    // (e.g. a client connected over telnet/WebUI after boot).
    if(hal.stream.write != NULL && hal.stream.write != ui_write) {
        ui_orig_write = hal.stream.write;
        hal.stream.write = ui_write;
    }

    task_add_delayed(ui_snapshot_task, NULL, 100);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const char *ui_state_name (uint8_t state)
{
    if(state & STATE_ALARM)
        return "Alarm";

    if(state & STATE_SAFETY_DOOR)
        return "Door";

    if(state & STATE_ESTOP)
        return "E-Stop";

    if(state & STATE_HOLD)
        return "Hold";

    if(state & STATE_JOG)
        return "Jog";

    if(state & STATE_HOMING)
        return "Homing";

    if(state & STATE_TOOL_CHANGE)
        return "Tool change";

    if(state & STATE_CYCLE)
        return "Run";

    if(state & STATE_CHECK_MODE)
        return "Check";

    if(state & STATE_SLEEP)
        return "Sleep";

    return "Idle";
}

static bool ui_query (httpd_req_t *req, char *buf, size_t len)
{
    return httpd_req_get_url_query_str(req, buf, len) == ESP_OK;
}

static void ui_url_decode (char *s)
{
    char *d = s;

    while(*s) {
        if(*s == '%' && s[1] && s[2]) {
            char hex[3] = { s[1], s[2], 0 };
            *d++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else if(*s == '+') {
            *d++ = ' ';
            s++;
        } else
            *d++ = *s++;
    }

    *d = '\0';
}

static bool ui_query_str (httpd_req_t *req, const char *key, char *out, size_t len)
{
    char query[256];

    if(!ui_query(req, query, sizeof(query)))
        return false;

    if(httpd_query_key_value(query, key, out, len) != ESP_OK)
        return false;

    ui_url_decode(out);

    return true;
}

static float ui_query_float (httpd_req_t *req, const char *key, float def)
{
    char value[32];

    return ui_query_str(req, key, value, sizeof(value)) ? (float)atof(value) : def;
}

static void ui_json_escape (const char *in, char *out, size_t len)
{
    size_t o = 0;

    while(*in && o + 7 < len) {
        unsigned char c = (unsigned char)*in++;
        switch(c) {

            case '"':
            case '\\':
                out[o++] = '\\';
                out[o++] = (char)c;
                break;

            case '\n':
                out[o++] = '\\';
                out[o++] = 'n';
                break;

            case '\r':
                out[o++] = '\\';
                out[o++] = 'r';
                break;

            case '\t':
                out[o++] = '\\';
                out[o++] = 't';
                break;

            default:
                if(c < 0x20) {
                    o += (size_t)snprintf(out + o, len - o, "\\u%04x", c);
                } else
                    out[o++] = (char)c;
        }
    }

    out[o < len ? o : len - 1] = '\0';
}

static esp_err_t ui_send_json (httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t ui_ok (httpd_req_t *req, bool ok, const char *msg)
{
    char buf[160];

    if(ok || msg == NULL)
        snprintf(buf, sizeof(buf), "{\"ok\":%s}", ok ? "true" : "false");
    else
        snprintf(buf, sizeof(buf), "{\"ok\":false,\"msg\":\"%s\"}", msg);

    return ui_send_json(req, buf);
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

static esp_err_t ui_root_handler (httpd_req_t *req)
{
    static const char token[] = "__BUILD__";
    const char *place = strstr(index_html, token);
    esp_err_t res;

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");

    if(place == NULL)
        return httpd_resp_send(req, index_html, (ssize_t)strlen(index_html));

    // Inject the build stamp so a served page always identifies its firmware.
    res = httpd_resp_send_chunk(req, index_html, (ssize_t)(place - index_html));
    if(res == ESP_OK)
        res = httpd_resp_send_chunk(req, BUILD_STAMP, (ssize_t)strlen(BUILD_STAMP));
    if(res == ESP_OK)
        res = httpd_resp_send_chunk(req, place + strlen(token), (ssize_t)strlen(place + strlen(token)));
    if(res == ESP_OK)
        res = httpd_resp_send_chunk(req, NULL, 0);

    return res;
}

static esp_err_t ui_status_handler (httpd_req_t *req)
{
    char buf[768];

    snprintf(buf, sizeof(buf),
             "{\"state\":\"%s\",\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,"
#if N_AXIS > 3
             "\"a\":%.3f,"
#endif
             "\"limits\":%u,\"limits_seen\":%u,\"alarm\":%u,\"last_alarm\":%u,"
             "\"probe\":%s,\"door\":%s,\"estop\":%s,"
             "\"spindle\":%s,\"wco_z\":%.3f,\"uptime\":%lu}",
             ui_state_name(ui_status.state),
             (double)ui_status.pos[X_AXIS], (double)ui_status.pos[Y_AXIS], (double)ui_status.pos[Z_AXIS],
#if N_AXIS > 3
             (double)ui_status.pos[3],
#endif
             (unsigned)ui_status.limits,
             (unsigned)ui_limits_seen,
             (unsigned)ui_status.alarm,
             (unsigned)ui_status.last_alarm,
             ui_status.probe ? "true" : "false",
             ui_status.door ? "true" : "false",
             ui_status.estop ? "true" : "false",
             ui_status.spindle ? "true" : "false",
             (double)ui_status.wco_z,
             (unsigned long)ui_status.uptime_ms);

    return ui_send_json(req, buf);
}

static esp_err_t ui_info_handler (httpd_req_t *req)
{
    const esp_app_desc_t *desc = esp_ota_get_app_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    char buf[896];

    if(running)
        esp_ota_get_state_partition(running, &state);

    snprintf(buf, sizeof(buf),
             "{\"version\":\"%s\",\"project\":\"%s\",\"idf\":\"%s\","
             "\"date\":\"%s\",\"time\":\"%s\",\"built\":\"%s\",\"api\":1,\"reset_reason\":%d,"
             "\"branch\":\"%s\",\"commit\":\"%s\",\"dirty\":\"%s\",\"core\":\"%s\",\"board\":\"%s\","
             "\"slot\":\"%s\",\"ota_state\":%d,\"free_heap\":%u,\"uptime\":%lu}",
             desc ? desc->version : "?",
             desc ? desc->project_name : "?",
             desc ? desc->idf_ver : "?",
             desc ? desc->date : "?",
             desc ? desc->time : "?",
             BUILD_STAMP,
             (int)esp_reset_reason(),
             BUILD_BRANCH, BUILD_COMMIT, BUILD_DIRTY, BUILD_CORE,
#ifdef BOARD_NAME
             BOARD_NAME,
#else
             "?",
#endif
             running ? running->label : "?",
             (int)state,
             (unsigned)esp_get_free_heap_size(),
             (unsigned long)ui_status.uptime_ms);

    return ui_send_json(req, buf);
}

static esp_err_t ui_command_handler (httpd_req_t *req)
{
    char line[192];
    int len = httpd_req_recv(req, line, sizeof(line) - 1);

    if(len <= 0)
        return ui_ok(req, false, "empty");

    line[len] = '\0';

    while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';

    if(len == 0)
        return ui_ok(req, false, "empty");

    return ui_ok(req, grbl.enqueue_gcode(line), "busy");
}

static esp_err_t ui_realtime_handler (httpd_req_t *req)
{
    char value[8];

    if(!ui_query_str(req, "c", value, sizeof(value)))
        return ui_ok(req, false, "missing c");

    return ui_ok(req, protocol_enqueue_realtime_command((char)strtol(value, NULL, 16)), "busy");
}

static esp_err_t ui_jog_handler (httpd_req_t *req)
{
    char axis[4] = "x";
    char line[64];
    float d = ui_query_float(req, "d", 1.0f);
    float feed = ui_query_float(req, "feed", 1000.0f);

    ui_query_str(req, "axis", axis, sizeof(axis));

    if((axis[0] != 'x' && axis[0] != 'y' && axis[0] != 'z' && axis[0] != 'a') ||
        d == 0.0f || d > 200.0f || d < -200.0f)
        return ui_ok(req, false, "bad args");

    snprintf(line, sizeof(line), "$J=G91 %c%.3f F%.0f",
             (char)(axis[0] & 0xDF), (double)d, (double)feed);

    return ui_ok(req, grbl.enqueue_gcode(line), "busy");
}

static esp_err_t ui_zero_handler (httpd_req_t *req)
{
    char axis[8] = "xy";

    ui_query_str(req, "axis", axis, sizeof(axis));

    if(axis[0] == 'z')
        return ui_ok(req, grbl.enqueue_gcode("G10 L20 P1 Z0"), "busy");

    if(axis[0] == 'a')
        return ui_ok(req, grbl.enqueue_gcode("G10 L20 P1 A0"), "busy");

    return ui_ok(req, grbl.enqueue_gcode("G10 L20 P1 X0 Y0"), "busy");
}

static esp_err_t ui_home_handler (httpd_req_t *req)
{
    return ui_ok(req, grbl.enqueue_gcode("$H"), "busy");
}

static esp_err_t ui_files_handler (httpd_req_t *req)
{
    char path[64] = "/";
    vfs_dir_t *dir;
    vfs_dirent_t *dirent;
    char *buf = malloc(UI_JSON_MAX);
    size_t pos = 0;
    unsigned count = 0;

    ui_query_str(req, "path", path, sizeof(path));

    if(buf == NULL)
        return ui_ok(req, false, "oom");

    pos += (size_t)snprintf(buf + pos, UI_JSON_MAX - pos,
                            "{\"ok\":true,\"path\":\"");

    {
        char esc[96];
        ui_json_escape(path, esc, sizeof(esc));
        pos += (size_t)snprintf(buf + pos, UI_JSON_MAX - pos, "%s", esc);
    }

    pos += (size_t)snprintf(buf + pos, UI_JSON_MAX - pos, "\",\"files\":[");

    if((dir = vfs_opendir(path)) != NULL) {

        while((dirent = vfs_readdir(dir)) != NULL && count < 60) {
            char esc[192];

            if(dirent->name[0] == '.')
                continue;

            ui_json_escape(dirent->name, esc, sizeof(esc));
            pos += (size_t)snprintf(buf + pos, UI_JSON_MAX - pos,
                                    "%s{\"n\":\"%s\",\"s\":%u,\"d\":%s}",
                                    count ? "," : "", esc,
                                    (unsigned)dirent->size,
                                    dirent->st_mode.directory ? "true" : "false");
            count++;
        }

        vfs_closedir(dir);
    }

    pos += (size_t)snprintf(buf + pos, UI_JSON_MAX - pos, "],\"count\":%u}", count);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, buf, (ssize_t)pos);
    free(buf);

    return ESP_OK;
}

static esp_err_t ui_files_upload_handler (httpd_req_t *req)
{
    char path[64] = "";
    char buf[512];
    vfs_file_t *file;
    size_t total = 0;
    int len;

    if(!ui_query_str(req, "path", path, sizeof(path)) || path[0] != '/')
        return ui_ok(req, false, "bad path");

    if((file = vfs_open(path, "w")) == NULL)
        return ui_ok(req, false, "cannot create");

    while((len = httpd_req_recv(req, buf, sizeof(buf))) > 0) {
        if(vfs_write(buf, 1, (size_t)len, file) != (size_t)len) {
            vfs_close(file);
            return ui_ok(req, false, "write failed");
        }
        total += (size_t)len;
    }

    vfs_close(file);

    return ui_ok(req, len == 0, "transfer error");
}

static esp_err_t ui_files_delete_handler (httpd_req_t *req)
{
    char path[64] = "";

    if(!ui_query_str(req, "path", path, sizeof(path)))
        return ui_ok(req, false, "bad path");

    return ui_ok(req, vfs_unlink(path) == 0, "delete failed");
}

static esp_err_t ui_diag_handler (httpd_req_t *req)
{
    static char buf[256];

    snprintf(buf, sizeof(buf),
             "{\"ok\":true,\"hook_installed\":%s,\"orig_set\":%s,\"head\":%u,\"tail\":%u,"
             "\"write_ptr\":%u,\"ui_write_ptr\":%u}",
             hal.stream.write == ui_write ? "true" : "false",
             ui_orig_write != NULL ? "true" : "false",
             (unsigned)ui_rb_head, (unsigned)ui_rb_tail,
             (unsigned)(uintptr_t)hal.stream.write,
             (unsigned)(uintptr_t)ui_write);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, buf);
}

static esp_err_t ui_pins_raw_handler (httpd_req_t *req)
{
    static const uint8_t pins[] = { 39, 40, 41, 48, 37, 38 };
    static const char *names[] = { "X", "Y", "Z", "A", "Probe", "Door" };
    static char buf[384];
    size_t pos = 0;
    size_t i;

    pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":true,\"pins\":[");

    for(i = 0; i < sizeof(pins); i++)
        pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos,
                                "%s{\"n\":\"%s\",\"gpio\":%u,\"l\":%d}",
                                i ? "," : "", names[i], (unsigned)pins[i],
                                gpio_get_level((gpio_num_t)pins[i]));

    pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, (ssize_t)pos);
}

static esp_err_t ui_alarm_clear_handler (httpd_req_t *req)
{
    ui_status.last_alarm = 0;
    return ui_ok(req, true, NULL);
}

static esp_err_t ui_limits_clear_handler (httpd_req_t *req)
{
    ui_limits_seen = 0;
    ui_limits_prev = ui_status.limits;
    return ui_ok(req, true, NULL);
}

static esp_err_t ui_sd_mount_handler (httpd_req_t *req)
{
    return ui_ok(req, grbl.enqueue_gcode("$FM"), "busy");
}

static esp_err_t ui_sd_unmount_handler (httpd_req_t *req)
{
    return ui_ok(req, grbl.enqueue_gcode("$FU"), "busy");
}

static esp_err_t ui_settings_handler (httpd_req_t *req)
{
    static char buf[1024];
    size_t pos = 0;
    uint8_t i;

    pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos,
                            "{\"ok\":true,\"axes\":[");

    for(i = 0; i < N_AXIS; i++) {
        pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos,
                                "%s{\"letter\":\"%c\",\"steps\":%.3f,\"rate\":%.1f,"
                                "\"accel\":%.1f,\"travel\":%.1f,\"dir_inv\":%u}",
                                i ? "," : "",
                                "XYZABCUVW"[i],
                                (double)settings.axis[i].steps_per_mm,
                                (double)settings.axis[i].max_rate,
                                (double)settings.axis[i].acceleration,
                                (double)settings.axis[i].max_travel,
                                (unsigned)((settings.steppers.dir_invert.mask >> i) & 1));
    }

    pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos,
                            "],\"step_invert\":%u,\"dir_invert\":%u,"
                            "\"limits\":{\"invert\":%u,\"hard\":%u,\"soft\":%u,\"homing\":%u,\"pullup_disable\":%u},"
                            "\"homingcfg\":{\"dir\":%u,\"pulloff\":%.2f,\"debounce\":%u,\"cycles\":%u,"
                            "\"feed\":%.1f,\"seek\":%.1f}}",
                            (unsigned)settings.steppers.step_invert.mask,
                            (unsigned)settings.steppers.dir_invert.mask,
                            (unsigned)settings.limits.invert.mask,
                            (unsigned)settings.limits.flags.hard_enabled,
                            (unsigned)settings.limits.soft_enabled.mask,
                            (unsigned)settings.homing.flags.enabled,
                            (unsigned)settings.limits.disable_pullup.mask,
                            (unsigned)settings.homing.dir_mask.mask,
                            (double)settings.homing.pulloff,
                            (unsigned)settings.homing.debounce_delay,
                            (unsigned)settings.homing.locate_cycles,
                            (double)settings.axis[0].homing_feed_rate,
                            (double)settings.axis[0].homing_seek_rate);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    return httpd_resp_send(req, buf, (ssize_t)pos);
}

static esp_err_t ui_heightmap_handler (httpd_req_t *req)
{
    const heightmap_t *hm = heightmap_get();
    char *buf = malloc(UI_JSON_MAX);
    size_t pos = 0;
    uint8_t ix, iy;

    if(buf == NULL)
        return ui_ok(req, false, "oom");

    if(hm == NULL) {
        snprintf(buf, UI_JSON_MAX, "{\"ok\":true,\"state\":\"%s\"}",
                 heightmap_state_name(heightmap_get_state()));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, buf);
        free(buf);
        return ESP_OK;
    }

    pos += (size_t)snprintf(buf + pos, UI_JSON_MAX - pos,
                            "{\"ok\":true,\"state\":\"%s\",\"n\":%u,"
                            "\"x0\":%.3f,\"y0\":%.3f,\"dx\":%.3f,\"dy\":%.3f,"
                            "\"wco_z\":%.3f,\"z\":[",
                            heightmap_state_name(heightmap_get_state()),
                            (unsigned)hm->n,
                            (double)hm->x0, (double)hm->y0,
                            (double)hm->dx, (double)hm->dy,
                            (double)ui_status.wco_z);

    for(ix = 0; ix < hm->n; ix++)
        for(iy = 0; iy < hm->n; iy++)
            pos += (size_t)snprintf(buf + pos, UI_JSON_MAX - pos, "%s%.4f",
                                    (ix || iy) ? "," : "",
                                    (double)(hm->machine_z[ix][iy] - ui_status.wco_z));

    pos += (size_t)snprintf(buf + pos, UI_JSON_MAX - pos, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, (ssize_t)pos);
    free(buf);

    return ESP_OK;
}

static esp_err_t ui_heightmap_action_handler (httpd_req_t *req)
{
    char action[16] = "";

    ui_query_str(req, "action", action, sizeof(action));

    if(!strcmp(action, "probe"))
        return ui_ok(req, grbl.enqueue_gcode("M102"), "busy");

    if(!strcmp(action, "clear"))
        return ui_ok(req, grbl.enqueue_gcode("M104"), "busy");

    return ui_ok(req, false, "bad action");
}

static esp_err_t ui_poll_handler (httpd_req_t *req)
{
    static char buf[2048];
    char out[768], esc[1024];
    size_t len = 0;
    char val[8];
    bool with_console = ui_query_str(req, "console", val, sizeof(val)) && val[0] == '1';
    uint32_t tail = ui_rb_tail;
    uint32_t head = ui_rb_head;

    if(with_console) {
        while(tail != head && len < sizeof(out) - 1) {
            out[len++] = ui_rb[tail];
            tail = (tail + 1) % UI_RB_SIZE;
        }
        ui_rb_tail = tail;
    }

    out[len] = '\0';
    ui_json_escape(out, esc, sizeof(esc));

    snprintf(buf, sizeof(buf),
             "{\"state\":\"%s\",\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,"
#if N_AXIS > 3
             "\"a\":%.3f,"
#endif
             "\"limits\":%u,\"limits_seen\":%u,\"alarm\":%u,\"last_alarm\":%u,"
             "\"probe\":%s,\"door\":%s,\"estop\":%s,"
             "\"spindle\":%s,\"wco_z\":%.3f,\"uptime\":%lu,"
             "\"out\":\"%s\"}",
             ui_state_name(ui_status.state),
             (double)ui_status.pos[X_AXIS], (double)ui_status.pos[Y_AXIS], (double)ui_status.pos[Z_AXIS],
#if N_AXIS > 3
             (double)ui_status.pos[3],
#endif
             (unsigned)ui_status.limits,
             (unsigned)ui_limits_seen,
             (unsigned)ui_status.alarm,
             (unsigned)ui_status.last_alarm,
             ui_status.probe ? "true" : "false",
             ui_status.door ? "true" : "false",
             ui_status.estop ? "true" : "false",
             ui_status.spindle ? "true" : "false",
             (double)ui_status.wco_z,
             (unsigned long)ui_status.uptime_ms,
             esc);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    return httpd_resp_sendstr(req, buf);
}

static esp_err_t ui_output_handler (httpd_req_t *req)
{
    char buf[1024];
    size_t len = 0;
    uint32_t tail = ui_rb_tail;
    uint32_t head = ui_rb_head;

    while(tail != head && len < sizeof(buf) - 1) {
        buf[len++] = ui_rb[tail];
        tail = (tail + 1) % UI_RB_SIZE;
    }

    ui_rb_tail = tail;
    buf[len] = '\0';

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    return httpd_resp_send(req, buf, (ssize_t)len);
}

#if OTA_ENABLE
static esp_err_t ui_fw_status_handler (httpd_req_t *req)
{
    return ota_http_status(req);
}

static esp_err_t ui_fw_post_handler (httpd_req_t *req)
{
    return ota_http_post(req);
}

static esp_err_t ui_fw_rollback_handler (httpd_req_t *req)
{
    return ui_ok(req, grbl.enqueue_gcode("$RB"), "busy");
}
#endif

// ---------------------------------------------------------------------------
// Server start
// ---------------------------------------------------------------------------

static void ui_start (void *arg)
{
    static uint8_t attempts = 0;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    static const httpd_uri_t routes[] = {
        { .uri = "/",              .method = HTTP_GET,    .handler = ui_root_handler },
        { .uri = "/api/status",    .method = HTTP_GET,    .handler = ui_status_handler },
        { .uri = "/api/info",      .method = HTTP_GET,    .handler = ui_info_handler },
        { .uri = "/api/command",   .method = HTTP_POST,   .handler = ui_command_handler },
        { .uri = "/api/realtime",  .method = HTTP_GET,    .handler = ui_realtime_handler },
        { .uri = "/api/jog",       .method = HTTP_GET,    .handler = ui_jog_handler },
        { .uri = "/api/zero",      .method = HTTP_GET,    .handler = ui_zero_handler },
        { .uri = "/api/home",      .method = HTTP_GET,    .handler = ui_home_handler },
        { .uri = "/api/files",     .method = HTTP_GET,    .handler = ui_files_handler },
        { .uri = "/api/files",     .method = HTTP_POST,   .handler = ui_files_upload_handler },
        { .uri = "/api/files",     .method = HTTP_DELETE, .handler = ui_files_delete_handler },
        { .uri = "/api/settings",   .method = HTTP_GET,     .handler = ui_settings_handler },
        { .uri = "/api/limits/clear", .method = HTTP_POST,   .handler = ui_limits_clear_handler },
        { .uri = "/api/alarm/clear", .method = HTTP_POST,   .handler = ui_alarm_clear_handler },
        { .uri = "/api/diag",      .method = HTTP_GET,     .handler = ui_diag_handler },
        { .uri = "/api/pins/raw",  .method = HTTP_GET,     .handler = ui_pins_raw_handler },
        { .uri = "/api/sd/mount",   .method = HTTP_POST,   .handler = ui_sd_mount_handler },
        { .uri = "/api/sd/unmount", .method = HTTP_POST,   .handler = ui_sd_unmount_handler },
        { .uri = "/api/heightmap", .method = HTTP_GET,    .handler = ui_heightmap_handler },
        { .uri = "/api/heightmap", .method = HTTP_POST,   .handler = ui_heightmap_action_handler },
        { .uri = "/api/output",    .method = HTTP_GET,    .handler = ui_output_handler },
        { .uri = "/api/poll",      .method = HTTP_GET,    .handler = ui_poll_handler },
#if OTA_ENABLE
        { .uri = "/api/fw/status", .method = HTTP_GET,    .handler = ui_fw_status_handler },
        { .uri = "/api/fw",        .method = HTTP_POST,   .handler = ui_fw_post_handler },
        { .uri = "/api/fw/rollback", .method = HTTP_POST, .handler = ui_fw_rollback_handler },
#endif
    };

    if(ui_server != NULL)
        return;

    config.server_port = UI_HTTP_PORT;
    config.ctrl_port = UI_HTTP_PORT + 1;
    config.stack_size = 10240;
    config.task_priority = 5;
    config.core_id = 0;                     // keep core 1 free for grbl
    config.max_uri_handlers = 40;
    config.uri_match_fn = httpd_uri_match_wildcard;

    if(httpd_start(&ui_server, &config) != ESP_OK) {
        ui_server = NULL;
        if(++attempts < 10)
            task_add_delayed(ui_start, NULL, 1000);
        else
            report_plugin("WebUI", "start failed");
        return;
    }

    {
        size_t i;
        for(i = 0; i < sizeof(routes) / sizeof(httpd_uri_t); i++)
            httpd_register_uri_handler(ui_server, &routes[i]);
    }

    report_plugin("WebUI", "2.0");
}

static void ui_start_deferred (void *arg)
{
    // Let lwIP/network stack come up first (same pattern as the OTA server).
    task_add_delayed(ui_start, NULL, 2000);
}

void ui_init (void)
{
    memset(&ui_status, 0, sizeof(ui_status));

    // Prefix the stream writer so the UI terminal can show replies.
    if(hal.stream.write != NULL) {
        ui_orig_write = hal.stream.write;
        hal.stream.write = ui_write;
    }

    task_run_on_startup(ui_start_deferred, NULL);
    task_add_delayed(ui_snapshot_task, NULL, 500);
}

#endif // UI_ENABLE
