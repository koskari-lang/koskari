# Makefile for koskari

PYTHON ?= $(shell which python3 python 2>/dev/null | head -n1)
PYTHON := $(PYTHON)

PROJECT := $(shell awk -F' = ' '/^\[metadata\]/{m=1} m && /^name = /{print $$2; exit}' setup.cfg)
VERSION := $(shell awk -F' = ' '/^\[metadata\]/{m=1} m && /^version = /{print $$2; exit}' setup.cfg)


# Build configuration
DEBUG ?= 0
BUILD_DIR := build
DIST_DIR := dist
KOSKARI_APP_MAIN := koskari/app/main.c
KOSKARI_BIN := $(BUILD_DIR)/koskari-bin/koskari
KOSKARI_NATIVE_SRC := $(shell find koskari/lib -type f \
	\( -name '*.c' -o -name '*.h' -o -name '*.incl' \) | sort)

define checkfor
	@if ! which $(1) >/dev/null 2>&1 ; then \
		echo "$(1) is required, but not available" 1>&2 ; \
		exit 1 ; \
	fi
endef


##@ Basic Targets

.DEFAULT_GOAL := help

help:  ## Display this help
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make \033[36m\033[0m\n"} /^[a-zA-Z0-9_-]+:.*?##/ { printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2 } /^##@/ { printf "\n\033[1m%s\033[0m\n", substr($$0, 5) } ' $(MAKEFILE_LIST)

report-python:  ## Report Python version and executable
	@echo "Using python: $(PYTHON)"
	@$(PYTHON) -VV


##@ Build and Install

koskari/lib/handlers.incl: koskari/lib/opcodes.incl
	@echo "Re-generating handlers.incl..."
	@tox -qe handlers

handlers: koskari/lib/handlers.incl

# Editor highlighting assets: each output is rebuilt only when spec, its
# template, or the generator script is newer than the output. Generation runs
# in the tools container (see build-tools) so the toolchain producing these
# committed files is pinned rather than whatever the contributor happens to
# have installed.
EDITOR_SPEC := tools/editor_tokens/spec.yml
EDITOR_REGEN := tools/editor_tokens/regen.ki

editors/textmate/syntaxes/koskari.tmLanguage.json: $(EDITOR_SPEC) editors/textmate/syntaxes/koskari.tmLanguage.json.j2 $(EDITOR_REGEN)
	@echo "Re-generating koskari.tmLanguage.json..."
	@$(TOOLS_RUN) tools/tools.ki -- editors $(EDITOR_SPEC) editors/textmate/syntaxes/koskari.tmLanguage.json.j2 $@

# Canonical tree-sitter highlight queries (shared by Helix and Neovim).
editors/tree-sitter/queries/highlights.scm: $(EDITOR_SPEC) editors/tree-sitter/queries/highlights.scm.j2 $(EDITOR_REGEN)
	@echo "Re-generating tree-sitter highlights.scm..."
	@$(TOOLS_RUN) tools/tools.ki -- editors $(EDITOR_SPEC) editors/tree-sitter/queries/highlights.scm.j2 $@

# Zed needs the queries inside its own extension dir; same template, same output.
editors/zed/languages/koskari/highlights.scm: $(EDITOR_SPEC) editors/tree-sitter/queries/highlights.scm.j2 $(EDITOR_REGEN)
	@echo "Re-generating Zed highlights.scm..."
	@$(TOOLS_RUN) tools/tools.ki -- editors $(EDITOR_SPEC) editors/tree-sitter/queries/highlights.scm.j2 $@

editors/emacs/koskari-mode.el: $(EDITOR_SPEC) editors/emacs/koskari-mode.el.j2 $(EDITOR_REGEN)
	@echo "Re-generating koskari-mode.el..."
	@$(TOOLS_RUN) tools/tools.ki -- editors $(EDITOR_SPEC) editors/emacs/koskari-mode.el.j2 $@

editors-grammar:  ## Regenerate the tree-sitter parser (src/parser.c) from grammar.js
	@echo "Re-generating tree-sitter parser..."
	@cd editors/tree-sitter && npm install --silent && npx tree-sitter generate

regen-editors: editors/textmate/syntaxes/koskari.tmLanguage.json editors/tree-sitter/queries/highlights.scm editors/zed/languages/koskari/highlights.scm editors/emacs/koskari-mode.el  ## Regenerate editor syntax-highlighting assets (incremental)

build: handlers clean-built report-python  ## Build wheel package using uv via tox
	@$(call checkfor,tox)
	@echo "Building $(PROJECT) $(VERSION)..."
	@tox -qe build
	@echo "Build complete: $(DIST_DIR)/$(PROJECT)-$(VERSION)-*.whl"

install: build  ## Install package for current user
	@echo "Installing $(PROJECT) $(VERSION)..."
	@$(PYTHON) -m pip install --user --no-deps $(DIST_DIR)/$(PROJECT)-$(VERSION)-*.whl
	@echo "Installation complete"

install-dev:  ## Install package in development mode
	@echo "Installing $(PROJECT) in development mode..."
	@$(PYTHON) -m pip install -e .
	@echo "Development installation complete"

koskari-bin: handlers report-python $(KOSKARI_BIN)  ## Build native koskari executable (build/koskari-bin/koskari)

$(KOSKARI_BIN): $(KOSKARI_APP_MAIN)
	@echo "Building native koskari executable..."
	@mkdir -p $(BUILD_DIR)
	@$(PYTHON) -B setup.py --egg-base $(BUILD_DIR) build_ext --inplace
	@mkdir -p $(BUILD_DIR)/koskari-bin
	@set -e; \
	CC="$${CC:-gcc}"; \
	PCFG="$$( $(PYTHON) -c "import os, sys; print(os.path.splitext(sys.executable)[0] + '-config')" )"; \
	if [ ! -x "$$PCFG" ]; then PCFG=python3-config; fi; \
	if [ ! -x "$$PCFG" ]; then PCFG=python-config; fi; \
	CFLAGS=$$("$$PCFG" --cflags --embed 2>/dev/null || "$$PCFG" --cflags); \
	LDFLAGS=$$("$$PCFG" --ldflags --embed 2>/dev/null || "$$PCFG" --ldflags); \
	$$CC $(KOSKARI_APP_MAIN) -o $@ $$CFLAGS $$LDFLAGS
	@echo "Native koskari built at $(KOSKARI_BIN)"

uninstall:  ## Uninstall package
	@echo "Uninstalling $(PROJECT)..."
	@$(PYTHON) -m pip uninstall -y $(PROJECT) || true
	@echo "Uninstallation complete"

##@ Development

test: handlers  ## Run tests on current Python version via tox
	@$(call checkfor,tox)
	@echo "Running tests..."
	@tox -qe test

test-corpus: handlers  ## Run only the corpus conformance phase (phase_corpus)
	@$(call checkfor,tox)
	@tox -qe py -- -m phase_corpus

tox: handlers  ## Run full tox suite (tests + lint)
	@$(call checkfor,tox)
	@echo "Running full tox suite..."
	@tox -q

gdb: handlers  ## Run tests under gdb via tox
	@$(call checkfor,tox)
	@$(call checkfor,gdb)
	@tox -qe gdb


##@ Container-based Memory Testing

CONTAINER_IMAGE := koskari-native-tests
CONTAINER_ENGINE := $(shell which podman 2>/dev/null || which docker 2>/dev/null || echo "podman")

test-container-build:  ## Build the native test container
	@echo "Building container image $(CONTAINER_IMAGE)..."
	@$(CONTAINER_ENGINE) build -q -t $(CONTAINER_IMAGE) -f tests/native/Containerfile tests/native/

test-native: test-container-build  ## Run all native tests in container
	@echo "Running all native memory tests in container..."
	@$(CONTAINER_ENGINE) run --rm -v $(PWD):/src:Z $(CONTAINER_IMAGE) normal

test-asan: test-container-build  ## Run ASan tests in container
	@echo "Running ASan tests in container..."
	@$(CONTAINER_ENGINE) run --rm -v $(PWD):/src:Z $(CONTAINER_IMAGE) asan

test-valgrind: test-container-build  ## Run Valgrind tests in container
	@echo "Running Valgrind tests in container..."
	@$(CONTAINER_ENGINE) run --rm -v $(PWD):/src:Z $(CONTAINER_IMAGE) valgrind


##@ Full-Python Valgrind

VALGRIND_IMAGE := koskari-valgrind

valgrind-image-build:  ## Build the koskari full-Python valgrind container
	@echo "Building container image $(VALGRIND_IMAGE)..."
	@$(CONTAINER_ENGINE) build -q -t $(VALGRIND_IMAGE) -f tools/valgrind/Containerfile .

valgrind-run: valgrind-image-build  ## Run RUN_ARGS under valgrind (e.g. make valgrind-run RUN_ARGS="koskari /work/test.ki")
	@$(CONTAINER_ENGINE) run --rm -it -v $(PWD):/work:Z $(VALGRIND_IMAGE) $(RUN_ARGS)

valgrind-koslife: valgrind-image-build  ## Run the koslife demo under valgrind
	@$(CONTAINER_ENGINE) run --rm -it -v $(PWD):/work:Z $(VALGRIND_IMAGE) koslife


##@ Static analysis

CPPCHECK_IMAGE ?= ghcr.io/cnescatlab/cppcheck:latest
CPPCHECK_REPORT ?= $(BUILD_DIR)/cppcheck.sarif

cppcheck:  ## Run cppcheck on koskari/lib; SARIF report written to CPPCHECK_REPORT
	@$(call checkfor,$(CONTAINER_ENGINE))
	@mkdir -p $(dir $(CPPCHECK_REPORT))
	@echo "Running cppcheck via $(CPPCHECK_IMAGE)..."
	@$(CONTAINER_ENGINE) run --rm \
		-v $(PWD):/src:Z \
		-w /src \
		$(CPPCHECK_IMAGE) \
		cppcheck \
			--output-format=sarif \
			--output-file=$(CPPCHECK_REPORT) \
			--enable=warning,style,performance,portability \
			--error-exitcode=1 \
			--inline-suppr \
			--suppress=missingIncludeSystem \
			-I koskari/lib \
			--suppressions-list=tools/cppcheck-suppressions.txt \
			koskari/lib/; \
	rc=$$?; \
	if command -v jq >/dev/null 2>&1 && test -f $(CPPCHECK_REPORT); then \
		echo ""; \
		jq -r -f tools/cppcheck-sarif-summary.jq $(CPPCHECK_REPORT); \
	fi; \
	echo ""; \
	echo "Full output saved to $(CPPCHECK_REPORT)"; \
	exit $$rc


##@ Multi-version Container Testing

RUN_ARGS ?=  ## Extra args for tox (e.g. make test-py312 RUN_ARGS="-it")

test-pyversions: handlers  ## Run tests in containers for all Python versions
	@$(call checkfor,$(CONTAINER_ENGINE))
	@tools/pyversions.sh $(CONTAINER_ENGINE) "" $(RUN_ARGS)

test-py39: handlers  ## Run tests in container for Python 3.9
	@$(call checkfor,$(CONTAINER_ENGINE))
	@tools/pyversions.sh $(CONTAINER_ENGINE) 3.9 $(RUN_ARGS)

test-py310: handlers  ## Run tests in container for Python 3.10
	@$(call checkfor,$(CONTAINER_ENGINE))
	@tools/pyversions.sh $(CONTAINER_ENGINE) 3.10 $(RUN_ARGS)

test-py311: handlers  ## Run tests in container for Python 3.11
	@$(call checkfor,$(CONTAINER_ENGINE))
	@tools/pyversions.sh $(CONTAINER_ENGINE) 3.11 $(RUN_ARGS)

test-py312: handlers  ## Run tests in container for Python 3.12
	@$(call checkfor,$(CONTAINER_ENGINE))
	@tools/pyversions.sh $(CONTAINER_ENGINE) 3.12 $(RUN_ARGS)

test-py313: handlers  ## Run tests in container for Python 3.13
	@$(call checkfor,$(CONTAINER_ENGINE))
	@tools/pyversions.sh $(CONTAINER_ENGINE) 3.13 $(RUN_ARGS)

test-py314: handlers  ## Run tests in container for Python 3.14
	@$(call checkfor,$(CONTAINER_ENGINE))
	@tools/pyversions.sh $(CONTAINER_ENGINE) 3.14 $(RUN_ARGS)


##@ Runtime Container

build-container:  ## Build koskari runtime container image
	@echo "Building koskari container image..."
	@build_args=$$(mktemp); \
	trap 'rm -f "$$build_args"' EXIT; \
	tox -qqe regen-version -- --report > "$$build_args"; \
	$(CONTAINER_ENGINE) build \
	  --build-arg-file "$$build_args" \
	  -t $(PROJECT):$(VERSION)-dev \
	  -t $(PROJECT):local \
	  -f Containerfile .; \
	echo "Container image $(PROJECT):$(VERSION)-dev built"


##@ Virtual Environment

.venv:
	@$(call checkfor,uv)
	@echo "Ensuring virtual environment..."
	@uv venv -q --clear


# Inputs whose *content* determines whether the extension must rebuild.
KOSKARI_BUILD_INPUTS := setup.py setup.cfg $(KOSKARI_NATIVE_SRC)

# Rebuild decision is content-based, not mtime-based. The recipe runs on every
# invocation (FORCE prereq) but reinstalls only when a hash of the build inputs
# differs from the stored hash; otherwise it exits immediately. This survives a
# git pull (or a tox rebuild) that lands sources with mtimes older than the
# venv, which defeats Make's normal timestamp comparison.
.venv/.koskari-built: FORCE | .venv
	@$(call checkfor,uv)
	@new=`cat $(KOSKARI_BUILD_INPUTS) | cksum`; \
	if [ -f $@ ] && [ "$$new" = "`cat .venv/.koskari-srchash 2>/dev/null`" ]; \
	then exit 0; fi; \
	echo "Installing koskari..."; \
	uv pip install -qe .[repl]; \
	printf '%s\n' "$$new" > .venv/.koskari-srchash; \
	touch $@


# Empty phony target used to force a recipe to run on every invocation
# (see the content-hash guard in .venv/.koskari-built).
FORCE:
.PHONY: FORCE


# Developer tool dependencies (tools/requirements.txt) are kept out of the
# package extras deliberately, and out of the koskari install recipe above so
# that bumping a tool dep does not force an extension rebuild. Same
# content-hash guard, for the same git-pull-mtime reason.
.venv/.tools-built: FORCE | .venv
	@$(call checkfor,uv)
	@new=`cat tools/requirements.txt | cksum`; \
	if [ -f $@ ] && [ "$$new" = "`cat .venv/.tools-srchash 2>/dev/null`" ]; \
	then exit 0; fi; \
	echo "Installing tool dependencies..."; \
	uv pip install -q -r tools/requirements.txt; \
	printf '%s\n' "$$new" > .venv/.tools-srchash; \
	touch $@


.venv/bin/koskari: .venv/.koskari-built
	@test -x $@


.venv/bin/koslife: .venv/bin/koskari
	@$(call checkfor,uv)
	@echo "Installing koslife..."
	@uv pip install -q setuptools
	@uv pip install -qe ./examples/life --no-build-isolation


venv: .venv/bin/koskari .venv/.tools-built ## Create virtual environment
	@echo "Virtual environment ready"
	@echo "   'make venv-activate' to activate it"
	@echo "   'uv run koskari' to run the REPL"


venv-activate: venv  ## Activate virtual environment
	@echo "Activating virtual environment..."
	@source .venv/bin/activate
	@echo "Virtual environment activated"


venv-koslife: .venv/bin/koslife ## Install koslife example
	@echo "koslife installed"
	@echo "   'uv run koslife' to run the life example"
	@echo
	@uv run koslife --help


##@ Corpus

CORPUS_IMAGE := koskari-corpus-export

corpus-parquet:  ## Export koskari-corpus parquet from corpus YAML via tox
	@$(call checkfor,tox)
	@echo "Exporting corpus view koskari-corpus..."
	@tox -qe regen-corpus -- --view koskari-corpus

corpus-parquet-all:  ## Export all corpus views to build/corpus/datasets/
	@$(call checkfor,tox)
	@echo "Exporting all corpus views..."
	@tox -qe regen-corpus -- --all-views

corpus-parquet-container-build:  ## Build corpus parquet export container image
	@echo "Building corpus export container image $(CORPUS_IMAGE)..."
	@$(CONTAINER_ENGINE) build -q -t $(CORPUS_IMAGE) -f tools/corpus/Containerfile .

corpus-parquet-container: corpus-parquet-container-build  ## Run corpus export container; writes to build/corpus/datasets/
	@mkdir -p $(BUILD_DIR)/corpus/datasets
	@$(CONTAINER_ENGINE) run --rm \
	  -v $(PWD)/$(BUILD_DIR)/corpus/datasets:/out:Z \
	  $(CORPUS_IMAGE)


##@ MCP

MCP_IMAGE := koskari-mcp

build-mcp:  ## Build koskari MCP server container (requires koskari:local)
	@echo "Building MCP server container image $(MCP_IMAGE)..."
	@$(CONTAINER_ENGINE) build -q -t $(MCP_IMAGE):local -f tools/mcp/Containerfile tools/mcp/

build-mcp-container: build-container build-mcp  ## Build koskari:local then the MCP server container

run-mcp:  ## Run MCP server on stdio (manual smoke test; use -i, no -t)
	@$(CONTAINER_ENGINE) run -i --rm $(MCP_IMAGE):local


##@ Developer Tools

TOOLS_IMAGE := koskari-tools

# Tool sources are not baked into the image; they arrive on the bind
# mount, so only a koskari or dependency change needs a rebuild.
TOOLS_RUN := $(CONTAINER_ENGINE) run --rm -v $(PWD):/work:Z $(TOOLS_IMAGE):local

build-tools:  ## Build koskari developer tools container (requires koskari:local)
	@echo "Building tools container image $(TOOLS_IMAGE)..."
	@$(CONTAINER_ENGINE) build -q -t $(TOOLS_IMAGE):local -f tools/Containerfile.tools tools/

build-tools-container: build-container build-tools  ## Build koskari:local then the tools container

run-tools-selftest:  ## Smoke test the tools dispatcher in the container
	@$(TOOLS_RUN) tools/tools.ki -- selftest


##@ Code Quality

lint:  ## Run linting (flake8) via tox
	@$(call checkfor,tox)
	@echo "Running flake8..."
	@tox -qe flake8

flawfinder:  ## Run Flawfinder on koskari/lib via tox
	@$(call checkfor,tox)
	@echo "Running flawfinder..."
	@tox -qe flawfinder

docs:  ## Build user documentation via tox
	@$(call checkfor,tox)
	@echo "Building documentation..."
	@tox -qe sphinx

docs-preview: docs  ## Build docs and serve build/site/docs for local preview
	@$(call checkfor,$(PYTHON))
	@echo "Serving documentation at http://127.0.0.1:8000/ (Ctrl+C to stop)"
	@cd $(BUILD_DIR)/site/docs && $(PYTHON) -m http.server 8000

site:  ## Build the full published site (landing page + docs) via tox
	@$(call checkfor,tox)
	@echo "Building full site..."
	@tox -qe site

site-preview: site  ## Build the full site and serve build/site for local preview
	@$(call checkfor,$(PYTHON))
	@echo "Serving site at http://127.0.0.1:8000/ (Ctrl+C to stop)"
	@cd $(BUILD_DIR)/site && $(PYTHON) -m http.server 8000

# Raster favicons / social assets regenerated from the canonical SVG.
# Committed under docs/_static; re-run when the icon artwork changes.
ICON_SVG := docs/_static/koskari-icon.svg
ICON_PNG_DIR := docs/_static
ICON_FONT_DIR := docs/_static/fonts
ICON_SIZES := 32 256
ICON_PNGS := $(foreach s,$(ICON_SIZES),$(ICON_PNG_DIR)/koskari-icon-$(s).png)
ICON_LOCKUP_SVG := $(ICON_PNG_DIR)/koskari-lockup.svg
ICON_LOCKUP_PNG := $(ICON_PNG_DIR)/koskari-lockup-64.png
ICON_FONTCONFIG := $(BUILD_DIR)/koskari-fonts.conf

icons: $(ICON_PNGS) $(ICON_LOCKUP_SVG) $(ICON_LOCKUP_PNG)  ## Regenerate PNG/SVG lockup from koskari-icon.svg

$(ICON_PNG_DIR)/koskari-icon-%.png: $(ICON_SVG)
	@$(call checkfor,rsvg-convert)
	@echo "Rendering $@..."
	@rsvg-convert -w $* -h $* -o $@ $<

# Private fontconfig so rsvg can see the vendored Bricolage Grotesque
# without installing it system-wide (rsvg ignores SVG @font-face).
$(ICON_FONTCONFIG): $(ICON_FONT_DIR)/BricolageGrotesque[opsz,wdth,wght].ttf
	@mkdir -p $(BUILD_DIR)/fontconfig-cache
	@printf '%s\n' \
		'<?xml version="1.0"?>' \
		'<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">' \
		'<fontconfig>' \
		'  <dir>$(abspath $(ICON_FONT_DIR))</dir>' \
		'  <cachedir>$(abspath $(BUILD_DIR)/fontconfig-cache)</cachedir>' \
		'  <include ignore_missing="yes">/etc/fonts/fonts.conf</include>' \
		'</fontconfig>' > $@
	@FONTCONFIG_FILE=$(abspath $@) fc-cache -f "$(abspath $(ICON_FONT_DIR))" >/dev/null 2>&1 || true

$(ICON_LOCKUP_SVG): $(ICON_SVG) tools/build_site.py
	@$(call checkfor,uv)
	@echo "Writing $@..."
	@uv run python3 -c 'from tools.build_site import write_lockup_svg; write_lockup_svg("$(ICON_LOCKUP_SVG)")'

$(ICON_LOCKUP_PNG): $(ICON_LOCKUP_SVG) $(ICON_FONTCONFIG)
	@$(call checkfor,rsvg-convert)
	@echo "Rendering $@ (wordmark)..."
	@FONTCONFIG_FILE=$(abspath $(ICON_FONTCONFIG)) rsvg-convert -h 64 -o $@ $(ICON_LOCKUP_SVG)


##@ Cleanup

CLEAN_DIRS := koskari tests tools examples

clean-built:  ## Remove build artifacts
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR) $(DIST_DIR) .eggs/*.egg-info/
	@if [ -f .venv/.koskari-built ]; then rm -f .venv/.koskari-built; fi

clean-ext:  ## Remove compiled extensions
	@echo "Cleaning compiled extensions..."
	@find $(CLEAN_DIRS) -type f -name "*.so" -delete 2>/dev/null || true
	@find $(CLEAN_DIRS) -type f -name "*.pyd" -delete 2>/dev/null || true
	@find $(CLEAN_DIRS) -type f -name "*.dylib" -delete 2>/dev/null || true
	@find koskari -type d -name "*.egg-info" -exec rm -rf {} + 2>/dev/null || true

clean-pyc:  ## Remove Python cache files
	@echo "Cleaning Python cache..."
	@find $(CLEAN_DIRS) -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
	@find $(CLEAN_DIRS) -type f -name "*.pyc" -delete 2>/dev/null || true
	@find $(CLEAN_DIRS) -type f -name "*.pyo" -delete 2>/dev/null || true

clean-native:  ## Remove native test build artifacts
	@$(MAKE) -C tests/native -f tests.mk clean

clean: clean-built clean-ext clean-pyc clean-native  ## Remove all build artifacts and caches
	@echo "Clean complete"

purge: clean  ## Deep clean (includes .tox, .venv, etc.)
	@rm -rf .tox .venv .mypy_cache .pytest_cache
	@rm -rf .coverage htmlcov/
	@echo "Purge complete"


##@ Information

project:  ## Show project name
	@echo $(PROJECT)

version:  ## Show project version
	@echo $(VERSION)

python:  ## Show Python executable
	@echo $(PYTHON)

.PHONY: \
	build \
	clean \
	corpus-parquet \
	corpus-parquet-all \
	corpus-parquet-container \
	corpus-parquet-container-build \
	clean-built \
	clean-ext \
	clean-native \
	clean-pyc \
	cppcheck \
	docs \
	editors-grammar \
	flawfinder \
	gdb \
	handlers \
	help \
	icons \
	build-mcp \
	build-mcp-container \
	run-mcp \
	install \
	install-dev \
	koskari-bin \
	lint \
	project \
	purge \
	python \
	regen-editors \
	report-python \
	site \
	site-preview \
	test \
	test-asan \
	test-container-build \
	test-native \
	test-py310 \
	test-py311 \
	test-py312 \
	test-py313 \
	test-py314 \
	test-py39 \
	test-pyversions \
	test-valgrind \
	tox \
	valgrind-image-build \
	valgrind-koslife \
	valgrind-run \
	uninstall \
	venv \
	version


# The end.
