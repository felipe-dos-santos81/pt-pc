# Makefile for P.T. PC Port — macOS build and run (docs/macos.md)
# Targets are numbered by build stage:
#   env → 1 configure → 2 build → 3 run → 4 app → dev
SERVICE = P.T. PC Port

# Variables
BUILD_DIR = build/macos
BREW_DEPS = cmake ninja shaderc vulkan-headers vulkan-loader
type ?= RelWithDebInfo

.PHONY: help install clean configure build run app test

# ── Environment ──────────────────────────────────────────────────────────────

help: ## Print this help message
	@printf '\033[01;32m${SERVICE} — macOS build and run\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target] [arg=\"val\"...]\n\n\033[33mTargets:\033[0m\n"
	@grep -E '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; \
		{printf "  \033[36m%-10s\033[0m %s\n", $$1, $$2}'

install: ## Install the missing Homebrew build dependencies
	@for dep in $(BREW_DEPS); do \
		brew list --versions $$dep >/dev/null 2>&1 || brew install $$dep || exit 1; \
	done; \
	echo "Environment setup complete."

# build/macos/data holds pt.ini, the saves and the remembered game folder of the build-folder runs.
clean: ## Remove the build output and the packaged app (keeps settings and saves)
	@if [ -d $(BUILD_DIR) ]; then find $(BUILD_DIR) -mindepth 1 -maxdepth 1 ! -name data -exec rm -rf {} +; fi
	rm -rf dist
	@echo "Cleanup complete."

# ── Stage 1 · Configure ──────────────────────────────────────────────────────

# The first configure downloads SDL, whisper.cpp, MoltenVK and the other pinned dependencies (a few minutes).
configure: install ## [STEP 1] Configure for this Mac's CPU (type=RelWithDebInfo)
	cmake -G Ninja -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(type)

$(BUILD_DIR)/build.ninja:
	$(MAKE) configure

# ── Stage 2 · Build ──────────────────────────────────────────────────────────

build: $(BUILD_DIR)/build.ninja ## [STEP 2] Build the game, configuring first when needed
	cmake --build $(BUILD_DIR) --target pt

# ── Stage 3 · Run ────────────────────────────────────────────────────────────

# Windowed is pt.ini's default and a free pointer is the macOS default. Without game=, the game looks for its folder
# and asks once when it finds none.
run: build ## [STEP 3] Run in a window (game=<extracted CUSA01127 folder> args="--mouse-lock ...")
	$(BUILD_DIR)/pt $(if $(game),--game "$(subst ~/,$(HOME)/,$(game))") $(args)

# ── Stage 4 · App bundle ─────────────────────────────────────────────────────

# make_app.py refuses an output that already exists.
app: build ## [STEP 4] Make dist/P.T. PC Port.app and its zip, replacing the last one
	rm -rf dist
	python3 tools/macos/make_app.py --build $(BUILD_DIR)

# ── Development ───────────────────────────────────────────────────────────────

test: $(BUILD_DIR)/build.ninja ## Build and run the pt_tests unit tests
	cmake --build $(BUILD_DIR) --target pt_tests
	$(BUILD_DIR)/pt_tests
