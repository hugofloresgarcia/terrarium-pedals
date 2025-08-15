
# Pedal project directories
PEDALS = wigglrs glitch cenote

.PHONY: all clean $(PEDALS)

# Default target: build everything
all: $(PEDALS)

# Build each pedal
$(PEDALS):
	$(MAKE) -C $@

# Clean everything
clean:
	for dir in $(PEDALS); do \
		$(MAKE) -C $$dir clean; \
	done
