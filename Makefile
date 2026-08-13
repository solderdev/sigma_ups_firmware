FQBN    := lattepanda:avr:lpleonardo
PORT    := /dev/ttyACM0
LIBRARY := ../DFRobot_LPUPS

.PHONY: help build flash

help:
	@echo "Usage: make <target>"
	@echo ""
	@echo "Targets:"
	@echo "  build   Compile the sketch (uses local library $(LIBRARY))"
	@echo "  flash   Upload the sketch to the board on $(PORT)"

build:
	arduino-cli compile --fqbn $(FQBN) --warnings all --library $(LIBRARY) .

flash:
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) .
