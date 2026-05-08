SOURCES := $(shell find solutions -maxdepth 1 -name '*.cpp' -type f 2>/dev/null | sort)
LATEST_SOURCE := $(shell find solutions -maxdepth 1 -name '*.cpp' -type f -exec ls -t {} + 2>/dev/null | sed -n '1p')
GOAL := $(or $(firstword $(MAKECMDGOALS)),run)
ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
ARG1 := $(firstword $(ARGS))
ARGREST := $(wordlist 2,$(words $(ARGS)),$(ARGS))
.DEFAULT_GOAL := run
P ?= $(ARG1)
ID ?= $(P)
PARTS ?= A-H
ifneq ($(strip $(LATEST_SOURCE)),)
SRC ?= $(LATEST_SOURCE)
else
SRC ?=
endif
RUN_ARG := $(if $(strip $(ID)),$(ID),$(SRC))

.PHONY: all run run-all bundle new install clean check-arg $(ARGS)

check-arg:
	@test -n "$(RUN_ARG)" || { \
		echo 'usage: make $(GOAL) 1985A'; \
		echo '   or: make $(GOAL) P=1985A'; \
		echo 'create problems with: make new 1985 A-H'; \
		exit 2; \
	}

run: check-arg
	./bin/run $(RUN_ARG)

all:
	./bin/run all

bundle: check-arg
	@./bin/bundle "$(RUN_ARG)"

new:
	@test -n "$(strip $(C)$(P))" || { \
		echo 'usage: make new 71A'; \
		echo '   or: make new 1985 A-H'; \
		exit 2; \
	}
	@if [ -n "$(strip $(C))" ]; then \
		if [ -n "$(strip $(P))" ]; then \
			./bin/new "$(C)" "$(P)"; \
		else \
			./bin/new "$(C)"; \
		fi; \
	else \
		./bin/new "$(P)" $(ARGREST); \
	fi

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
	@echo 'ensure $$HOME/.local/bin is in PATH'

clean:
	rm -rf .build

$(ARGS):
	@:
