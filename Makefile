PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man
MAN1DIR ?= $(MANDIR)/man1

CFX := ./bin/cfx
CFX_PATH := $(abspath $(CFX))
MANPAGE := man/cfx.1
TEST_RUNNER := tests/run.sh
TEST_SCRIPTS := $(shell find tests -type f -name '*.sh' -print)
RELEASE_SCRIPTS := $(shell find scripts/release -type f -name '*.sh' -print)
BROWSER_SCRIPTS := $(shell find browser -type f -name '*.sh' -print)

ifeq ($(origin CXX),default)
ifneq ($(wildcard /opt/homebrew/opt/llvm/bin/clang++),)
CXX := /opt/homebrew/opt/llvm/bin/clang++
else ifneq ($(wildcard /usr/local/opt/llvm/bin/clang++),)
CXX := /usr/local/opt/llvm/bin/clang++
endif
endif
ifneq ($(strip $(CFX_CXX)),)
CXX := $(CFX_CXX)
endif

CFX_STD ?= c++20
TOOL_BUILD_DIR := .build/tools
TOOL_BINARY := $(TOOL_BUILD_DIR)/cfx
TOOL_CONFIG := $(TOOL_BUILD_DIR)/cfx.config
TOOL_SOURCES := $(wildcard tools/cfx/*.cpp)
TOOL_OBJECTS := $(patsubst tools/cfx/%.cpp,$(TOOL_BUILD_DIR)/%.o,$(TOOL_SOURCES))
TOOL_DEPENDENCIES := $(TOOL_OBJECTS:.o=.d)
TOOL_CPPFLAGS := -Iinclude -Itools -Itools/cfx
TOOL_CXXFLAGS := -std=$(CFX_STD) -Wall -Wextra -Wpedantic -pthread

.DEFAULT_GOAL := help

.PHONY: help build test lint release-check verify browser-package install clean FORCE

help:
	@echo 'Development:'
	@echo '  make build        build the C++ command-line tool'
	@echo '  make test         run library and tooling tests'
	@echo '  make lint         check scripts and CLI startup'
	@echo '  make release-check  verify distributable packaging'
	@echo '  make verify       run all checks'
	@echo '  make browser-package  build the Chrome Web Store ZIP'
	@echo
	@echo 'Installation:'
	@echo '  make install      install cfx'
	@echo '  make clean        remove disposable build output'

build: $(TOOL_BINARY)

$(TOOL_CONFIG): FORCE
	@mkdir -p $(@D)
	@{ \
		printf '%s\n' 'CXX=$(CXX)'; \
		printf '%s\n' 'CFX_STD=$(CFX_STD)'; \
		printf '%s\n' 'CPPFLAGS=$(CPPFLAGS)'; \
		printf '%s\n' 'TOOL_CPPFLAGS=$(TOOL_CPPFLAGS)'; \
		printf '%s\n' 'CXXFLAGS=$(CXXFLAGS)'; \
		printf '%s\n' 'TOOL_CXXFLAGS=$(TOOL_CXXFLAGS)'; \
		printf '%s\n' 'LDFLAGS=$(LDFLAGS)'; \
		printf '%s\n' 'LDLIBS=$(LDLIBS)'; \
	} >$(TOOL_CONFIG).tmp
	@if cmp -s $(TOOL_CONFIG).tmp $(TOOL_CONFIG) 2>/dev/null; then \
		rm -f $(TOOL_CONFIG).tmp; \
	else \
		mv $(TOOL_CONFIG).tmp $(TOOL_CONFIG); \
	fi

$(TOOL_BUILD_DIR)/%.o: tools/cfx/%.cpp $(TOOL_CONFIG)
	@mkdir -p $(@D)
	@echo '  CXX  $<'
	@$(CXX) $(CPPFLAGS) $(TOOL_CPPFLAGS) $(CXXFLAGS) $(TOOL_CXXFLAGS) \
		-MMD -MP -c $< -o $@

$(TOOL_BINARY): $(TOOL_OBJECTS)
	@echo '  LINK $@'
	@$(CXX) $(LDFLAGS) $(TOOL_OBJECTS) -pthread $(LDLIBS) -o $@.tmp
	@mv $@.tmp $@

-include $(TOOL_DEPENDENCIES)

test: build
	bash $(TEST_RUNNER)

lint:
	bash -n $(CFX) $(TEST_SCRIPTS) $(RELEASE_SCRIPTS) $(BROWSER_SCRIPTS)
	@if command -v node >/dev/null 2>&1; then \
		for script in browser/*.js; do node --check "$$script"; done; \
		node -e 'JSON.parse(require("node:fs").readFileSync("browser/manifest.json", "utf8"))'; \
	fi
	@if command -v mandoc >/dev/null 2>&1; then mandoc -T lint $(MANPAGE); fi
	$(CFX) --help >/dev/null

release-check: build
	bash scripts/release/test.sh

verify: test lint release-check

browser-package:
	bash browser/package.sh

install:
	@test -x "$(CFX_PATH)" || { echo 'missing executable: $(CFX_PATH)' >&2; exit 1; }
	@test -f "$(MANPAGE)" || { echo 'missing manual: $(MANPAGE)' >&2; exit 1; }
	mkdir -p "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(MAN1DIR)"
	@dst="$(DESTDIR)$(BINDIR)/cfx"; \
	target=$$(readlink "$$dst" 2>/dev/null || true); \
	if { [ -e "$$dst" ] || [ -L "$$dst" ]; } && [ "$$target" != "$(CFX_PATH)" ]; then \
		echo "refusing to overwrite: $$dst" >&2; \
		exit 1; \
	fi; \
	ln -sfn "$(CFX_PATH)" "$$dst"
	install -m 0644 "$(MANPAGE)" "$(DESTDIR)$(MAN1DIR)/cfx.1"
	@echo 'installed $(BINDIR)/cfx and $(MAN1DIR)/cfx.1'

clean:
	rm -rf .build
