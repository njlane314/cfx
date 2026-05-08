SOURCES := $(shell find solutions -maxdepth 1 -name '*.cpp' -type f 2>/dev/null | sort)
LATEST_SOURCE := $(shell find solutions -maxdepth 1 -name '*.cpp' -type f -exec ls -t {} + 2>/dev/null | sed -n '1p')
GOAL := $(or $(firstword $(MAKECMDGOALS)),run)
ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
.DEFAULT_GOAL := run
ifneq ($(strip $(LATEST_SOURCE)),)
SRC ?= $(LATEST_SOURCE)
else
SRC ?=
endif
RUN_ARGS := $(if $(strip $(ARGS)),$(ARGS),$(SRC))

.PHONY: all run run-all bundle new install clean check-run-arg check-new-arg $(ARGS)

check-run-arg:
	@test -n "$(RUN_ARGS)" || { \
		echo 'usage: make $(GOAL) A 1985'; \
		echo 'create problems with: make new A 1985'; \
		exit 2; \
	}

check-new-arg:
	@test -n "$(strip $(ARGS))" || { \
		echo 'usage: make new A 71'; \
		echo '   or: make new A-H 1985'; \
		exit 2; \
	}

run: check-run-arg
	./bin/run $(RUN_ARGS)

all:
	./bin/run all

bundle: check-run-arg
	@./bin/bundle $(RUN_ARGS)

new: check-new-arg
	./bin/new $(ARGS)

install:
	mkdir -p $$HOME/.local/bin
	@set -e; for cmd in bundle run new; do \
		src="$(CURDIR)/bin/$$cmd"; \
		dst="$$HOME/.local/bin/$$cmd"; \
		target=$$(readlink "$$dst" 2>/dev/null || true); \
		if [ -e "$$dst" ] && [ "$$target" != "$$src" ]; then \
			echo "refusing to overwrite: $$dst"; \
			exit 1; \
		fi; \
		ln -sf "$$src" "$$dst"; \
	done
	@if [ "$$(readlink "$$HOME/.local/bin/submit" 2>/dev/null || true)" = "$(CURDIR)/bin/submit" ]; then \
		rm -f "$$HOME/.local/bin/submit"; \
	fi
	@echo 'ensure $$HOME/.local/bin is in PATH'

clean:
	rm -rf .build

$(ARGS):
	@:
