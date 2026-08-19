FQBN    := lattepanda:avr:lpleonardo
PORT    := /dev/ttyACM0
LIBRARY := libs/DFRobot_LPUPS

.PHONY: help build flash monitor

help:
	@echo "Usage: make <target>"
	@echo ""
	@echo "Targets:"
	@echo "  build    Compile the sketch (uses local library $(LIBRARY))"
	@echo "  flash    Upload the sketch to the board on $(PORT), then monitor"
	@echo "  monitor  Open serial monitor on $(PORT)"

build:
	arduino-cli compile --fqbn $(FQBN) --warnings all --library $(LIBRARY) ups_firmware

flash:
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) ups_firmware
	@sleep 2
	$(MAKE) monitor

monitor:
	arduino-cli monitor -p $(PORT)
