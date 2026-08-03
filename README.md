<h1 align="center">
  <img src="logo.png" width="64" align="left"/>
  KaleidoBox
</h1>

ESP32-S3 firmware driving a 64×64 HUB75 RGB LED matrix panel. Draw or upload an image, display it straight or as an animated kaleidoscope, save to a TF card gallery, and rotate through a clock, custom text messages, weather, and 3D-printer progress (via [PrintSpy](https://github.com/ccmpbll/printspy) over MQTT).

- **Hardware:** Waveshare ESP32-S3-RGB-Matrix + Waveshare RGB-Matrix-P2-64x64
- **Framework:** Pure ESP-IDF, no Arduino. HUB75 driving is isolated in `main/matrix.cpp`, wrapping `esphome/esp-hub75`
- **License:** MIT
- **Versioning:** Date-based (`vYYYY.MM.DD[a-z]`)

## Features

- Draw (touch/mouse) or upload a photo, decoded/resized/dithered on-device
- Kaleidoscope: animated polar-fold transform over the current canvas
- TF card gallery: save, cycle (auto or manual), delete
- Display rotation: cycles clock, gallery, custom messages, and takeovers (printer/weather)
- Custom text messages, word-wrapped and centered
- PrintSpy MQTT integration: takes over the panel to show name/progress/time-left/ETA while a tracked printer is running
- Weather (OpenWeatherMap): periodic takeover showing city/condition/temp/feels-like/high-low/humidity/wind
- Brightness schedule: auto-dim/brighten at configured times of day
- Boot splash, OTA firmware updates, web-based settings/backup

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
│   ├── main.c, board_pins.h         # Entry point, HUB75/TF card pin map
│   ├── wifi.c/.h, wifi_ap.c/.h      # WiFi provisioning
│   ├── settings.c/.h                # NVS-backed settings
│   ├── ota.c/.h, log.c/.h           # OTA update, log ring buffer + SSE
│   ├── matrix.cpp, matrix.h         # HUB75 driver wrapper (C API, no Arduino)
│   ├── image_decode.c/.h            # JPEG/PNG decode, resize, dither
│   ├── canvas.c/.h                  # Draw-mode pixel buffer
│   ├── kaleidoscope.c/.h            # Polar-fold animation transform
│   ├── sdcard.c/.h, gallery.c/.h    # TF card mount, saved-image list/cycle
│   ├── font_5x7.c/.h                # Bitmap font + text/overlay drawing
│   ├── clock.c/.h                   # Clock overlay/mode
│   ├── display_rotation.c/.h        # Slot rotation between display modes
│   ├── panel_takeover.c/.h          # Shared begin/end for exclusive takeovers
│   ├── printspy.c/.h                # MQTT printer status → panel takeover
│   ├── weather.c/.h                 # OpenWeatherMap fetch → panel takeover
│   ├── brightness_schedule.c/.h     # Time-of-day auto brightness
│   └── http_server.c/.h             # Web app + API
├── web/app.html, web/settings.html  # Mobile-first web app + settings page
├── partitions-8m.csv
└── sdkconfig.defaults*
```

## Web API

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | web app |
| GET | `/settings` | settings page |
| GET | `/api/status` | version/IP/SSID/uptime/current mode |
| POST | `/api/wifi` | update creds → reboot |
| POST | `/api/ota` | firmware upload → reboot |
| GET | `/api/logs` | SSE log console |
| WS | `/ws/draw` | instant-draw pixel stream |
| POST | `/api/canvas/submit` | draw-then-submit full-grid push |
| GET/POST | `/api/kaleidoscope` | fold count, motion mode, on/off |
| GET | `/api/gallery` | list saved images |
| POST | `/api/gallery/save` | save current buffer to TF card |
| POST | `/api/gallery/upload/:name` | image upload → decode/resize/dither → save |
| DELETE | `/api/gallery/:name` | remove image |
| POST | `/api/gallery/mode` | auto-advance (+interval) vs manual |
| POST | `/api/gallery/next` / `/prev` | manual cycle |
| GET/POST | `/api/clock` | mode/color/scale/24h/NTP/timezone |
| GET/POST | `/api/message` | custom text message settings |
| GET/POST | `/api/rotation` | display rotation slot config |
| GET/POST | `/api/printspy` | MQTT broker + PrintSpy topic/rotation settings |
| GET/POST | `/api/weather` | OpenWeatherMap key/city/units/fields/timing |
| GET/POST | `/api/brightness` | manual brightness + dim/bright schedule |
