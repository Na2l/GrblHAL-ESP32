/*
  ui.h - embedded web UI (grblHAL ESP32 driver)

  Serves a self-contained single page app on port 80 with a small JSON API.
  Replaces the stock ESP3D-WebUI page.
*/

#ifndef __UI_H__
#define __UI_H__

#ifndef UI_ENABLE
#define UI_ENABLE 0
#endif

void ui_init (void);

#endif // __UI_H__
