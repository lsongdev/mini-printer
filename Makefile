# CI and local development share these build entry points.
# PYTHON must be an interpreter with PlatformIO installed for packaging.
PIO ?= pio
PYTHON ?= python3
OUTPUT ?= dist
ALLOW_DIRTY ?=

.PHONY: all build test package

all: build

build:
	$(PIO) run -d firmware -e esp32dev

test:
	$(PIO) test -d firmware -e native
	$(PYTHON) -m unittest discover -s python -p 'test_*.py'

package: build
	$(PYTHON) tools/package_firmware.py --output $(OUTPUT) $(if $(ALLOW_DIRTY),--allow-dirty)
