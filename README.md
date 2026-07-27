# KaleidoBox

ESP32-S3 firmware driving a 64×64 HUB75 RGB LED matrix panel. Draw your own image or upload a picture from a phone/iPad, display it straight or as an animated kaleidoscope pattern, and save images to a TF card as a cycling gallery.

- **Hardware:** Waveshare ESP32-S3-RGB-Matrix + Waveshare RGB-Matrix-P2-64x64
- **Framework:** Pure ESP-IDF, no Arduino. The HUB75 matrix driver is isolated in one component (`main/matrix.cpp`) wrapping `esphome/esp-hub75` — confirmed as the right dependency via Waveshare's own official ESP-IDF example for this board
- **License:** MIT
- **Versioning:** Date-based

## Status

Scaffolding — buildable skeleton with WiFi provisioning, settings, OTA, and logging ported from [printspy-cam](https://github.com/ccmpbll/printspy-cam). HUB75/TF card pin mapping is confirmed against Waveshare's own official ESP-IDF example ([waveshareteam/ESP32-S3-RGB-Matrix](https://github.com/waveshareteam/ESP32-S3-RGB-Matrix)), but matrix driving, image decode, kaleidoscope rendering, and TF card gallery are all still stubbed pending real driver bring-up. See `notes/kaleidobox.md` in the parent workspace for full project notes and open questions.

## Building

```
make build
make flash
make monitor
```

## Project structure

```
kaleidobox/
├── main/
│   ├── main.c
│   ├── wifi.c/.h, wifi_ap.c/.h      # WiFi provisioning (ported from printspy-cam)
│   ├── settings.c/.h                # NVS-backed settings
│   ├── ota.c/.h, log.c/.h           # OTA update, in-memory log ring buffer + SSE
│   ├── board_pins.h                 # HUB75/TF card pin map - confirmed against Waveshare's official BSP
│   ├── matrix.h, matrix.cpp         # HUB75 driver wrapper (C API over esphome/esp-hub75, no Arduino)
│   ├── image_decode.c/.h            # JPEG/PNG decode, resize, dither
│   ├── canvas.c/.h                  # Draw-mode pixel buffer
│   ├── kaleidoscope.c/.h            # Polar-fold animation transform
│   ├── sdcard.c/.h                  # TF card mount
│   ├── gallery.c/.h                 # Saved-image list/cycle
│   └── http_server.c/.h             # Web app + API
├── web/app.html                     # Mobile-first web app (draw/upload/kaleidoscope/gallery)
├── partitions-8m.csv
└── sdkconfig.defaults*
```

## Web API

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | web app |
| GET | `/api/status` | version/IP/SSID/uptime/current mode |
| POST | `/api/wifi` | update creds → reboot |
| POST | `/api/ota` | firmware upload → reboot |
| GET | `/api/logs` | SSE log console |
| WS | `/ws/draw` | instant-draw pixel stream |
| POST | `/api/canvas/submit` | draw-then-submit full-grid push |
| POST | `/api/upload` | image upload → decode/resize/dither → display |
| GET/POST | `/api/kaleidoscope` | fold count, motion mode, on/off |
| GET | `/api/gallery` | list saved images |
| POST | `/api/gallery/save` | save current buffer to TF card |
| DELETE | `/api/gallery/:name` | remove image |
| POST | `/api/gallery/mode` | auto-advance (+interval) vs manual |
| POST | `/api/gallery/next` / `/prev` | manual cycle |
