SOURCES := $(shell find solutions -maxdepth 1 -name '*.cpp' -type f 2>/dev/null | sort)
LATEST_SOURCE := $(shell find solutions -maxdepth 1 -name '*.cpp' -type f -exec ls -t {} + 2>/dev/null | sed -n '1p')
GOAL := $(or $(firstword $(MAKECMDGOALS)),run)
.DEFAULT_GOAL := run
P ?=
ID ?= $(P)
PARTS ?= A-H
PROBLEM ?=
ifneq ($(strip $(ID)),)
SRC ?= solutions/$(ID).cpp
else
ifneq ($(strip $(LATEST_SOURCE)),)
SRC ?= $(LATEST_SOURCE)
else
SRC ?=
endif
endif
RUN_ARG := $(if $(strip $(ID)),$(ID),$(SRC))
BUILD_ID := $(if $(strip $(ID)),$(ID),$(basename $(notdir $(SRC))))

.PHONY: all run run-all bundle new submit install clean check-src

check-src:
	@test -n "$(SRC)" || { \
		echo 'usage: make $(GOAL) P=1985A'; \
		echo '   or: make $(GOAL) SRC=solutions/1985A.cpp'; \
		echo 'create problems with: make new C=1985 P=A-H'; \
		exit 2; \
	}
	@test -f "$(SRC)" || { \
		echo 'source not found: $(SRC)'; \
		echo 'create it with: make new C=1985 P=A-H'; \
		exit 2; \
	}

run: check-src
	./bin/run $(RUN_ARG)

all:
	./bin/run all

bundle: check-src
	mkdir -p submissions
	./bin/bundle $(RUN_ARG) > submissions/$(BUILD_ID).cpp
	@echo submissions/$(BUILD_ID).cpp

new:
	@test -n "$(strip $(C)$(P))" || { \
		echo 'usage: make new P=71A'; \
		echo '   or: make new C=1985 P=A-H'; \
		exit 2; \
	}
	@if [ -n "$(strip $(C))" ]; then \
		if [ -n "$(strip $(P))" ]; then \
			./bin/new "$(C)" "$(P)"; \
		else \
			./bin/new "$(C)"; \
		fi; \
	else \
		./bin/new "$(P)"; \
	fi

submit: check-src
	./bin/submit $(RUN_ARG) $(PROBLEM)

install:
	mkdir -p $$HOME/.local/bin
	@set -e; for cmd in bundle run new submit; do \
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
	rm -rf .build submissions
