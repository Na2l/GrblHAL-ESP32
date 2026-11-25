/*
  ota.h - A/B firmware update over WiFi (push) for grblHAL ESP32 driver

  Endpoints (own HTTP server so the WebUI plugin is untouched):
    POST /ota          body = app-only .bin (dlc32-max.bin), header
                       "X-OTA-Password: <OTA_PASSWORD>" if configured
    GET  /ota/status   JSON: running partition/version, boot partition,
                       rollback state, slot sizes

  Safety: image validated (ESP32-S3 chip id + app descriptor + project name),
  size checked against the inactive slot, written only to the inactive
  partition, boot pointer switched only after esp_ota_end() verifies the
  image, and the new image must mark itself valid (rollback otherwise).
*/

#ifndef __OTA_H__
#define __OTA_H__

#ifndef OTA_ENABLE
#define OTA_ENABLE 0
#endif

#if OTA_ENABLE
#include "esp_http_server.h"
esp_err_t ota_http_status (httpd_req_t *req);
esp_err_t ota_http_post (httpd_req_t *req);
#endif

void ota_init (void);

#endif // __OTA_H__
