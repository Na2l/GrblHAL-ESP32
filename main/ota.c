/*
  ota.c - A/B firmware update over WiFi (push) for grblHAL ESP32 driver

  See ota.h. Design goals: never brick, never accept the wrong image, never
  update while the machine is doing something.

  Update sequence:
    POST /ota (app-only .bin)
      -> auth (X-OTA-Password, if OTA_PASSWORD set)
      -> machine must be Idle and not streaming
      -> Content-Length sanity vs inactive slot size
      -> first 0x60 bytes validated: image magic, ESP32-S3 chip id,
         app descriptor magic, project name == "grbl"
      -> esp_ota_begin() on the INACTIVE slot, streamed in 4K chunks
      -> esp_ota_end() (verifies image), esp_ota_set_boot_partition(), reboot
  Failure at any point: esp_ota_abort(), running image and boot pointer
  untouched. Power loss during write leaves the inactive slot dirty only;
  power loss after the boot switch is covered by rollback
  (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) - the new image must call
  esp_ota_mark_app_valid_cancel_rollback() after a health delay.
*/

#include "driver.h"

#if OTA_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_app_format.h"
#include "esp_attr.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_partition.h"

#include "driver/gpio.h"

#include "grbl/hal.h"
#include "grbl/report.h"
#include "grbl/state_machine.h"
#include "grbl/system.h"
#include "grbl/task.h"

#include "ota.h"

#ifndef OTA_HTTP_PORT
#define OTA_HTTP_PORT 8080
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""                    // set to a secret string to require auth
#endif
#ifndef OTA_HEALTH_DELAY_MS
#define OTA_HEALTH_DELAY_MS 10000          // mark new image valid after this delay
#endif

#define OTA_BOOT_BUTTON_GPIO 0
#define OTA_ROLLBACK_HOLD_MS 3000
#define OTA_BOOTLOOP_LIMIT 3

RTC_NOINIT_ATTR static uint32_t ota_rtc_magic;
RTC_NOINIT_ATTR static uint32_t ota_rtc_boots;

#define OTA_CHUNK_SIZE 4096
#define OTA_HEADER_SIZE 0x60               // image header + app descriptor + project name
#define OTA_MIN_IMAGE_SIZE 0x10000
#define OTA_APP_DESC_MAGIC 0xABCD5432u
#define OTA_CHIP_ID_ESP32S3 9

static httpd_handle_t ota_server = NULL;
static volatile bool ota_in_progress = false;
static char ota_msg_buf[128];

static const char *ota_ok_json = "{\"ok\":true}";

static void ota_log (const char *msg)
{
    strncpy(ota_msg_buf, msg, sizeof(ota_msg_buf) - 1);
    ota_msg_buf[sizeof(ota_msg_buf) - 1] = '\0';
    report_message(ota_msg_buf, Message_Info);
}

static bool ota_auth_ok (httpd_req_t *req)
{
    if(*OTA_PASSWORD == '\0')
        return true;

    char value[64];
    size_t len = httpd_req_get_hdr_value_len(req, "X-OTA-Password");

    if(len == 0 || len >= sizeof(value))
        return false;

    if(httpd_req_get_hdr_value_str(req, "X-OTA-Password", value, sizeof(value)) != ESP_OK)
        return false;

    return strcmp(value, OTA_PASSWORD) == 0;
}

static bool ota_validate_image_header (const uint8_t *data, size_t len)
{
    const esp_app_desc_t *desc;

    if(len < OTA_HEADER_SIZE)
        return false;

    // ESP image header: magic, segment count, ...
    if(data[0] != 0xE9)
        return false;

    // chip id is at offset 12 (little endian), ESP32-S3 = 9
    if((uint16_t)(data[12] | (data[13] << 8)) != OTA_CHIP_ID_ESP32S3)
        return false;

    // app descriptor starts at offset 0x20, must be an *application* image
    desc = (const esp_app_desc_t *)&data[0x20];

    if(desc->magic_word != OTA_APP_DESC_MAGIC)
        return false;

    // reject images built for another project (e.g. a merged bootloader image)
    if(strncmp(desc->project_name, "grbl", sizeof(desc->project_name)) != 0)
        return false;

    return true;
}

static const esp_partition_t *ota_other_slot (void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();

    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY,
                                    running && !strcmp(running->label, "ota_0") ? "ota_1" : "ota_0");
}

static bool ota_fallback_valid (void)
{
    const esp_partition_t *other = ota_other_slot();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;

    return other != NULL &&
            esp_ota_get_state_partition(other, &state) == ESP_OK &&
             state == ESP_OTA_IMG_VALID;
}

static void ota_rollback (void *arg)
{
    const esp_partition_t *other;
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;

    if(!ota_fallback_valid()) {
        ota_log("OTA: rollback unavailable (no valid fallback slot)");
        return;
    }

    ota_log("OTA: rolling back to the previous firmware");

    if((other = ota_other_slot()) != NULL &&
        esp_ota_get_state_partition(other, &state) == ESP_OK &&
         state == ESP_OTA_IMG_VALID)
            esp_ota_mark_app_invalid_rollback_and_reboot();
}

// Called from boot (driver init): count boots that did not reach the health gate.
static void ota_bootloop_check (void *arg)
{
    if(ota_rtc_magic != 0x5A17B00Fu) {
        ota_rtc_magic = 0x5A17B00Fu;
        ota_rtc_boots = 0;
    }

    if(ota_rtc_boots < 0xFFFF)
        ota_rtc_boots++;

    if(ota_rtc_boots > OTA_BOOTLOOP_LIMIT) {
        ota_log("OTA: boot loop detected, rolling back");
        ota_rollback(NULL);
        ota_rtc_boots = 0;                 // fallback failed: do not retry every boot
    }
}

// Sample the BOOT button (GPIO0) while running: hold 3 s -> rollback.
static void ota_button_task (void *arg)
{
    static uint32_t held_ms = 0;

    if(gpio_get_level(OTA_BOOT_BUTTON_GPIO) == 0) {
        held_ms += 250;
        if(held_ms >= OTA_ROLLBACK_HOLD_MS) {
            held_ms = 0;
            if(state_get() == STATE_IDLE && hal.stream.file == NULL)
                task_add_immediate(ota_rollback, NULL);
            return;
        }
    } else
        held_ms = 0;

    task_add_delayed(ota_button_task, NULL, 250);
}

static status_code_t ota_cmd_rollback (sys_state_t state, char *args)
{
    task_add_immediate(ota_rollback, NULL);

    return Status_OK;
}

static status_code_t ota_cmd_slots (sys_state_t state, char *args)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *other = ota_other_slot();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    char msg[96];

    if(running && esp_ota_get_state_partition(running, &st) == ESP_OK)
        snprintf(msg, sizeof(msg), "running %s state %d, fallback %s %s",
                 running->label, (int)st,
                 other ? other->label : "?",
                 ota_fallback_valid() ? "valid" : "NOT valid");
    else
        snprintf(msg, sizeof(msg), "running %s, fallback %s",
                 running ? running->label : "?", other ? other->label : "?");

    ota_log(msg);

    return Status_OK;
}

esp_err_t ota_http_status (httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    const esp_partition_t *other = ota_other_slot();
    const esp_app_desc_t *desc = esp_ota_get_app_description();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED, fallback_state = ESP_OTA_IMG_UNDEFINED;
    esp_app_desc_t other_desc;
    char json[640];

    if(running)
        esp_ota_get_state_partition(running, &state);
    if(other)
        esp_ota_get_state_partition(other, &fallback_state);

    if(other == NULL || esp_ota_get_partition_description(other, &other_desc) != ESP_OK)
        memset(&other_desc, 0, sizeof(other_desc));

    snprintf(json, sizeof(json),
             "{\"ok\":true,\"in_progress\":%s,\"running\":\"%s\",\"boot\":\"%s\","
             "\"next\":\"%s\",\"version\":\"%s\",\"project\":\"%s\",\"state\":%d,"
             "\"slot_size\":%u,"
             "\"fallback\":{\"label\":\"%s\",\"state\":%d,\"version\":\"%s\",\"date\":\"%s\"}}",
             ota_in_progress ? "true" : "false",
             running ? running->label : "?",
             boot ? boot->label : "?",
             next ? next->label : "none",
             desc ? desc->version : "?",
             desc ? desc->project_name : "?",
             (int)state,
             next ? (unsigned)next->size : 0u,
             other ? other->label : "?",
             (int)fallback_state,
             other_desc.version,
             other_desc.date);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static void ota_reboot_task (void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

esp_err_t ota_http_post (httpd_req_t *req)
{
    const esp_partition_t *target;
    esp_ota_handle_t handle = 0;
    esp_err_t err;
    uint8_t *buf = NULL;
    uint8_t header[OTA_HEADER_SIZE];
    size_t header_len = 0, total = 0;
    long content_len = -1;
    char value[24];

    if(!ota_auth_ok(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"auth\"}");
    }

    if(ota_in_progress) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"update in progress\"}");
    }

    if(state_get() != STATE_IDLE || hal.stream.file != NULL) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"machine busy\"}");
    }

    if(httpd_req_get_hdr_value_str(req, "Content-Length", value, sizeof(value)) == ESP_OK)
        content_len = strtol(value, NULL, 10);

    if(content_len < OTA_MIN_IMAGE_SIZE) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing/short Content-Length\"}");
    }

    if((target = esp_ota_get_next_update_partition(NULL)) == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"no OTA partition\"}");
    }

    if((size_t)content_len > target->size) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"image larger than slot\"}");
    }

    if((buf = malloc(OTA_CHUNK_SIZE)) == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"out of memory\"}");
    }

    ota_in_progress = true;
    ota_log("OTA: update started");

    if((err = esp_ota_begin(target, (size_t)content_len, &handle)) != ESP_OK) {
        ota_log("OTA: esp_ota_begin failed");
        ota_in_progress = false;
        free(buf);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"ota begin\"}");
    }

    while(total < (size_t)content_len) {

        int len = httpd_req_recv(req, (char *)buf, OTA_CHUNK_SIZE);

        if(len == HTTPD_SOCK_ERR_TIMEOUT)
            continue;

        if(len <= 0) {
            ota_log("OTA: receive failed");
            goto fail;
        }

        // Accumulate and validate the image header before writing anything.
        if(header_len < OTA_HEADER_SIZE) {

            size_t copy = (size_t)len;

            if(copy > OTA_HEADER_SIZE - header_len)
                copy = OTA_HEADER_SIZE - header_len;

            memcpy(header + header_len, buf, copy);
            header_len += copy;

            if(header_len == OTA_HEADER_SIZE && !ota_validate_image_header(header, header_len)) {
                ota_log("OTA: rejected image (wrong chip/image/project)");
                goto fail;
            }
        }

        if((err = esp_ota_write(handle, buf, (size_t)len)) != ESP_OK) {
            ota_log("OTA: write failed");
            goto fail;
        }

        total += (size_t)len;
    }

    if(header_len < OTA_HEADER_SIZE) {
        ota_log("OTA: image too short");
        goto fail;
    }

    if((err = esp_ota_end(handle)) != ESP_OK) {
        ota_log("OTA: image verification failed");
        goto fail;
    }

    if((err = esp_ota_set_boot_partition(target)) != ESP_OK) {
        ota_log("OTA: set boot partition failed");
        goto fail;
    }

    free(buf);
    ota_log("OTA: update complete, rebooting");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"rebooting\":true}");

    xTaskCreate(ota_reboot_task, "ota_reboot", 2048, NULL, 5, NULL);

    return ESP_OK;

fail:
    esp_ota_abort(handle);
    ota_in_progress = false;
    free(buf);
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"update failed\"}");
}

// Called from the grbl task after boot: confirm the new image is healthy so the
// bootloader does not roll it back. If this never runs (crash/loop) rollback wins.
static void ota_mark_valid (void *data)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;

    if(running == NULL || esp_ota_get_state_partition(running, &state) != ESP_OK)
        return;

    // Nothing to do if the running image is already confirmed; otherwise
    // confirm it (NEW and PENDING_VERIFY both require this when rollback is
    // enabled, and a plain USB-flashed image has no otadata entry at all).
    if(state == ESP_OTA_IMG_VALID)
        return;

    if(esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
        ota_log("OTA: new image confirmed");
    else
        ota_log("OTA: failed to confirm new image");

    ota_rtc_boots = 0;                     // reached the health gate: not a boot loop
    ota_rtc_magic = 0x5A17B00Fu;
}

static void ota_start_server (void *data);

static void ota_start_server (void *data)
{
    static uint8_t attempts = 0;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_uri_t status_uri = {
        .uri = "/ota/status",
        .method = HTTP_GET,
        .handler = ota_http_status,
        .user_ctx = NULL
    };
    httpd_uri_t post_uri = {
        .uri = "/ota",
        .method = HTTP_POST,
        .handler = ota_http_post,
        .user_ctx = NULL
    };

    if(ota_server != NULL)
        return;

    if(esp_ota_get_next_update_partition(NULL) == NULL) {
        report_plugin("OTA", "disabled");
        return;
    }

    // Recovery hardening: boot-loop counter, BOOT button rollback, $RB/$RS.
    ota_bootloop_check(NULL);

    {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << OTA_BOOT_BUTTON_GPIO,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io);
    }

    {
        PROGMEM static const sys_command_t ota_cmd_list[] = {
            { "RB", ota_cmd_rollback, { .noargs = On }, { .str = "rollback to previous firmware" } },
            { "RS", ota_cmd_slots,    { .noargs = On }, { .str = "report firmware slots" } }
        };
        static sys_commands_t ota_commands = {
            .n_commands = sizeof(ota_cmd_list) / sizeof(sys_command_t),
            .commands = ota_cmd_list
        };
        system_register_commands(&ota_commands);
    }

    task_add_delayed(ota_button_task, NULL, 1000);

    // lwIP must be up before esp_http_server is started: retry from the grbl
    // task until the network stack is ready (WiFi/AP init happens after
    // driver_init in some configurations).
    config.server_port = OTA_HTTP_PORT;
    config.ctrl_port = OTA_HTTP_PORT + 1;
    config.stack_size = 8192;
    config.task_priority = 5;
    config.core_id = 0;                       // keep core 1 free for the grbl task
    config.max_uri_handlers = 4;
    config.uri_match_fn = httpd_uri_match_wildcard;

    if(httpd_start(&ota_server, &config) != ESP_OK) {
        ota_server = NULL;
        if(++attempts < 10)
            task_add_delayed(ota_start_server, NULL, 1000);
        else
            report_plugin("OTA", "server start failed");
        return;
    }

    httpd_register_uri_handler(ota_server, &status_uri);
    httpd_register_uri_handler(ota_server, &post_uri);

    report_plugin("OTA", "0.1");

    if(*OTA_PASSWORD == '\0')
        ota_log("OTA: WARNING no password configured (set OTA_PASSWORD)");

    // Health gate for a freshly booted (pending verify) image: give the
    // firmware time to come up before cancelling rollback.
    task_add_delayed(ota_mark_valid, NULL, OTA_HEALTH_DELAY_MS);
}

void ota_init (void)
{
    // Deferred: the network stack may not be initialized yet at this point.
    task_run_on_startup(ota_start_server, NULL);
}

#endif // OTA_ENABLE
