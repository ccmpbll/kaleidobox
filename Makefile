.PHONY: build flash monitor menuconfig clean

# Single board (Waveshare ESP32-S3-RGB-Matrix) - no per-board BOARD= variable
# needed, unlike printspy-cam which supports several camera boards.
SDKCONFIG_DEFAULTS = sdkconfig.defaults;sdkconfig.defaults.esp32s3

build:
	idf.py -D IDF_TARGET=esp32s3 -D SDKCONFIG_DEFAULTS="$(SDKCONFIG_DEFAULTS)" build

flash:
	idf.py -D IDF_TARGET=esp32s3 -D SDKCONFIG_DEFAULTS="$(SDKCONFIG_DEFAULTS)" flash

monitor:
	idf.py monitor

menuconfig:
	idf.py -D IDF_TARGET=esp32s3 -D SDKCONFIG_DEFAULTS="$(SDKCONFIG_DEFAULTS)" menuconfig

clean:
	idf.py fullclean
