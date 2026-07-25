PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

PROBS := ./bin/probs
PROBS_PATH := $(abspath $(PROBS))
TEST_RUNNER := tests/run.sh

.DEFAULT_GOAL := help

.PHONY: help build test lint verify install clean

help:
	@echo 'Development:'
	@echo '  make build        build the C++ command-line tool'
	@echo '  make test         run library and tooling tests'
	@echo '  make lint         check scripts and CLI startup'
	@echo '  make verify       run all checks'
	@echo
	@echo 'Installation:'
	@echo '  make install      install probs'
	@echo '  make clean        remove disposable build output'

build:
	$(PROBS) --help >/dev/null

test: build
	bash $(TEST_RUNNER)

lint:
	bash -n $(PROBS) $(TEST_RUNNER)
	@if command -v node >/dev/null 2>&1; then \
		node --check browser/connector.js; \
		node -e 'JSON.parse(require("node:fs").readFileSync("browser/manifest.json", "utf8"))'; \
	fi
	$(PROBS) --help >/dev/null

verify: test lint

install:
	@test -x "$(PROBS_PATH)" || { echo 'missing executable: $(PROBS_PATH)' >&2; exit 1; }
	mkdir -p "$(DESTDIR)$(BINDIR)"
	@dst="$(DESTDIR)$(BINDIR)/probs"; \
	target=$$(readlink "$$dst" 2>/dev/null || true); \
	if { [ -e "$$dst" ] || [ -L "$$dst" ]; } && [ "$$target" != "$(PROBS_PATH)" ]; then \
		echo "refusing to overwrite: $$dst" >&2; \
		exit 1; \
	fi; \
	ln -sfn "$(PROBS_PATH)" "$$dst"
	@for old in bundle run rerun new _local stress probe bench case fail _cf pick contest seen meta rank solved sample cc submit; do \
		dst="$(DESTDIR)$(BINDIR)/$$old"; \
		target=$$(readlink "$$dst" 2>/dev/null || true); \
		if [ "$$target" = "$(CURDIR)/bin/$$old" ]; then \
			rm -f "$$dst"; \
			echo "removed legacy link: $$dst"; \
		fi; \
	done
	@echo 'installed $(BINDIR)/probs'

clean:
	rm -rf .build
