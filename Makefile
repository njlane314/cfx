PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

CFX := ./bin/cfx
CFX_PATH := $(abspath $(CFX))
TEST_RUNNER := tests/run.sh

.DEFAULT_GOAL := help

.PHONY: help build test lint verify browser-package install clean

help:
	@echo 'Development:'
	@echo '  make build        build the C++ command-line tool'
	@echo '  make test         run library and tooling tests'
	@echo '  make lint         check scripts and CLI startup'
	@echo '  make verify       run all checks'
	@echo '  make browser-package  build the Chrome Web Store ZIP'
	@echo
	@echo 'Installation:'
	@echo '  make install      install cfx'
	@echo '  make clean        remove disposable build output'

build:
	$(CFX) --help >/dev/null

test: build
	bash $(TEST_RUNNER)

lint:
	bash -n $(CFX) $(TEST_RUNNER) browser/package.sh
	@if command -v node >/dev/null 2>&1; then \
		node --check browser/background.js; \
		node --check browser/connector.js; \
		node -e 'JSON.parse(require("node:fs").readFileSync("browser/manifest.json", "utf8"))'; \
	fi
	$(CFX) --help >/dev/null

verify: test lint

browser-package:
	bash browser/package.sh

install:
	@test -x "$(CFX_PATH)" || { echo 'missing executable: $(CFX_PATH)' >&2; exit 1; }
	mkdir -p "$(DESTDIR)$(BINDIR)"
	@dst="$(DESTDIR)$(BINDIR)/cfx"; \
	target=$$(readlink "$$dst" 2>/dev/null || true); \
	if { [ -e "$$dst" ] || [ -L "$$dst" ]; } && [ "$$target" != "$(CFX_PATH)" ]; then \
		echo "refusing to overwrite: $$dst" >&2; \
		exit 1; \
	fi; \
	ln -sfn "$(CFX_PATH)" "$$dst"
	@for old in probs bundle run rerun new _local stress probe bench case fail _cf pick contest seen meta rank solved sample cc submit; do \
		dst="$(DESTDIR)$(BINDIR)/$$old"; \
		target=$$(readlink "$$dst" 2>/dev/null || true); \
		if [ "$$target" = "$(CURDIR)/bin/$$old" ]; then \
			rm -f "$$dst"; \
			echo "removed legacy link: $$dst"; \
		fi; \
	done
	@echo 'installed $(BINDIR)/cfx'

clean:
	rm -rf .build
