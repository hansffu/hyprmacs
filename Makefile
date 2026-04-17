SHELL := bash

BUILD_DIR ?= build/plugin
PLUGIN_SO := $(BUILD_DIR)/hyprmacs.so
CMAKE ?= cmake
CTEST ?= ctest
EMACS ?= emacs

.PHONY: all build test load clean

all: build

build:
	$(CMAKE) -S plugin -B $(BUILD_DIR)
	$(CMAKE) --build $(BUILD_DIR)

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure
	$(EMACS) -Q --batch -L emacs -L tests/emacs -l bootstrap-tests.el -f ert-run-tests-batch-and-exit
	$(EMACS) -Q --batch -L emacs -L tests/emacs -l buffer-tests.el -f ert-run-tests-batch-and-exit
	$(EMACS) -Q --batch -L emacs -L tests/emacs -l session-tests.el -f ert-run-tests-batch-and-exit
	$(EMACS) -Q --batch -L emacs -L tests/emacs -l protocol-tests.el -f ert-run-tests-batch-and-exit
	$(EMACS) -Q --batch -L emacs -L tests/emacs -l layout-tests.el -f ert-run-tests-batch-and-exit

load: build
	@test -n "$$HYPRLAND_INSTANCE_SIGNATURE" || (echo "HYPRLAND_INSTANCE_SIGNATURE is not set. Run this inside Hyprland." && exit 1)
	@test -f "$(PLUGIN_SO)" || (echo "Missing plugin artifact: $(PLUGIN_SO)" && exit 1)
	@if strings "$(PLUGIN_SO)" | grep -Eq "bootstrap-fallback|Built without Hyprland headers"; then \
		echo "Refusing to load fallback plugin build from $(PLUGIN_SO)"; \
		echo "Build in an environment with Hyprland headers available."; \
		exit 1; \
	fi
	hyprctl plugin load "$(abspath $(PLUGIN_SO))"

clean:
	rm -rf build/plugin
