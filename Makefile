SOURCES := $(shell find solutions -maxdepth 1 -name '*.cpp' -type f 2>/dev/null | sort)
LATEST_SOURCE := $(shell find solutions -maxdepth 1 -name '*.cpp' -type f -exec ls -t {} + 2>/dev/null | sed -n '1p')
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man
MAN1 := man/build.1 man/test.1 man/data.1
MAN7 := man/probs.7
GOAL := $(or $(firstword $(MAKECMDGOALS)),run)
ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
ABSORB_ARGS := $(filter run rerun check bundle new stress probe bench case fail pick contest seen meta rank solved sample cc,$(GOAL))
.DEFAULT_GOAL := run
ifneq ($(strip $(LATEST_SOURCE)),)
SRC ?= $(LATEST_SOURCE)
else
SRC ?=
endif
RUN_ARGS := $(if $(strip $(ARGS)),$(ARGS),$(SRC))
LOCAL_ENV_VAR = $(if $(filter command line environment,$(origin $(1))),$(1)='$($(1))')
LOCAL_ENV := $(foreach v,N GEN BRUTE SEED GENARGS TL GENTL VERBOSE SHOW_BYTES CXX STD CXXFLAGS CHECK,$(call LOCAL_ENV_VAR,$(v)))

.PHONY: run rerun check bundle new stress probe bench case fail pick contest seen meta rank solved sample cc man check-man install install-man clean check-run-arg check-new-arg

ifneq ($(strip $(ABSORB_ARGS)),)
.PHONY: $(ARGS)
$(ARGS):
	@:
else
.PHONY: all
all:
	./bin/run all
endif

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

rerun: check-run-arg
	./bin/rerun $(RUN_ARGS)

check: check-run-arg
	CHECK=1 ./bin/run $(RUN_ARGS)

bundle: check-run-arg
	@./bin/bundle $(RUN_ARGS)

new: check-new-arg
	./bin/new $(ARGS)

stress:
	$(LOCAL_ENV) ./bin/stress $(ARGS)

probe:
	$(LOCAL_ENV) ./bin/probe $(ARGS)

bench:
	$(LOCAL_ENV) ./bin/bench $(ARGS)

case:
	$(LOCAL_ENV) ./bin/case $(ARGS)

fail:
	$(LOCAL_ENV) ./bin/fail $(ARGS)

pick:
	./bin/pick $(ARGS)

contest:
	./bin/contest $(ARGS)

seen:
	./bin/seen $(ARGS)

meta:
	./bin/meta $(ARGS)

rank:
	./bin/rank $(ARGS)

solved:
	./bin/solved $(ARGS)

sample:
	./bin/sample $(ARGS)

cc:
	./bin/cc $(ARGS)

man:
	@set -e; for page in $(MAN1) $(MAN7); do \
		echo "$$page"; \
		mandoc -Tutf8 "$$page" >/dev/null; \
	done

check-man:
	mandoc -Tlint -Wwarning $(MAN1) $(MAN7)

install:
	mkdir -p "$(DESTDIR)$(BINDIR)"
	@set -e; for cmd in bundle run rerun new _local stress probe bench case fail _cf pick contest seen meta rank solved sample cc; do \
		src="$(CURDIR)/bin/$$cmd"; \
		dst="$(DESTDIR)$(BINDIR)/$$cmd"; \
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
	$(MAKE) install-man
	@echo 'ensure $(BINDIR) is in PATH'

install-man: check-man
	mkdir -p "$(DESTDIR)$(MANDIR)/man1" "$(DESTDIR)$(MANDIR)/man7"
	install -m 644 $(MAN1) "$(DESTDIR)$(MANDIR)/man1"
	install -m 644 $(MAN7) "$(DESTDIR)$(MANDIR)/man7"

clean:
	rm -rf .build
