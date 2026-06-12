# Makefile for koskari

PYTHON ?= $(shell which python3 python 2>/dev/null | head -n1)
PYTHON := $(PYTHON)

PROJECT := $(shell $(PYTHON) -B setup.py --name)
VERSION := $(shell $(PYTHON) -B setup.py --version)


# Build configuration
DEBUG ?= 0
BUILD_DIR := build
DIST_DIR := dist

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
# template, or the generator script is newer than the output.
EDITOR_SPEC := tools/editor-tokens/spec.yml
EDITOR_REGEN := tools/editor-tokens/regen_editors_assets.py

editors/textmate/syntaxes/koskari.tmLanguage.json: $(EDITOR_SPEC) editors/textmate/syntaxes/koskari.tmLanguage.json.j2 $(EDITOR_REGEN)
	@$(call checkfor,tox)
	@echo "Re-generating koskari.tmLanguage.json..."
	@tox -qe regen-editors -- $(EDITOR_SPEC) editors/textmate/syntaxes/koskari.tmLanguage.json.j2 editors/textmate/syntaxes/koskari.tmLanguage.json

editors/helix/tree-sitter-koskari/queries/highlights.scm: $(EDITOR_SPEC) editors/helix/tree-sitter-koskari/queries/highlights.scm.j2 $(EDITOR_REGEN)
	@$(call checkfor,tox)
	@echo "Re-generating highlights.scm..."
	@tox -qe regen-editors -- $(EDITOR_SPEC) editors/helix/tree-sitter-koskari/queries/highlights.scm.j2 editors/helix/tree-sitter-koskari/queries/highlights.scm

editors/emacs/koskari-mode.el: $(EDITOR_SPEC) editors/emacs/koskari-mode.el.j2 $(EDITOR_REGEN)
	@$(call checkfor,tox)
	@echo "Re-generating koskari-mode.el..."
	@tox -qe regen-editors -- $(EDITOR_SPEC) editors/emacs/koskari-mode.el.j2 editors/emacs/koskari-mode.el

regen-editors: editors/textmate/syntaxes/koskari.tmLanguage.json editors/helix/tree-sitter-koskari/queries/highlights.scm editors/emacs/koskari-mode.el  ## Regenerate editor syntax-highlighting assets (incremental)

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

uninstall:  ## Uninstall package
	@echo "Uninstalling $(PROJECT)..."
	@$(PYTHON) -m pip uninstall -y $(PROJECT) || true
	@echo "Uninstallation complete"

##@ Development

test: handlers  ## Run tests on current Python version via tox
	@$(call checkfor,tox)
	@echo "Running tests..."
	@tox -qe test

tox: handlers  ## Run full tox suite (tests + lint + typecheck)
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
	  -f Containerfile .; \
	echo "Container image $(PROJECT):$(VERSION)-dev built"


##@ Corpus

CORPUS_IMAGE := koskari-corpus-export

corpus-parquet:  ## Export koskari-eval parquet from corpus YAML via tox
	@$(call checkfor,tox)
	@echo "Exporting corpus view koskari-eval..."
	@tox -qe regen-corpus -- --view koskari-eval

corpus-parquet-container-build:  ## Build corpus parquet export container image
	@echo "Building corpus export container image $(CORPUS_IMAGE)..."
	@$(CONTAINER_ENGINE) build -q -t $(CORPUS_IMAGE) -f tools/corpus/Containerfile .

corpus-parquet-container: corpus-parquet-container-build  ## Run corpus export container; writes to build/corpus/datasets/
	@mkdir -p $(BUILD_DIR)/corpus/datasets
	@$(CONTAINER_ENGINE) run --rm \
	  -v $(PWD)/$(BUILD_DIR)/corpus/datasets:/out:Z \
	  $(CORPUS_IMAGE)


##@ Code Quality

lint:  ## Run linting (flake8) via tox
	@$(call checkfor,tox)
	@echo "Running flake8..."
	@tox -qe flake8

typecheck:  ## Run type checking (mypy) via tox
	@$(call checkfor,tox)
	@echo "Running mypy..."
	@tox -qe mypy

docs:  ## Build user documentation via tox
	@$(call checkfor,tox)
	@echo "Building documentation..."
	@tox -qe sphinx

docs-preview: docs  ## Build docs and serve build/docs for local preview
	@$(call checkfor,$(PYTHON))
	@echo "Serving documentation at http://127.0.0.1:8000/ (Ctrl+C to stop)"
	@cd $(BUILD_DIR)/docs && $(PYTHON) -m http.server 8000


##@ Cleanup

clean-built:  ## Remove build artifacts
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)/ $(DIST_DIR)/ *.egg-info/

clean-ext:  ## Remove compiled extensions
	@echo "Cleaning compiled extensions..."
	@find . -type f -name "*.so" -delete
	@find . -type f -name "*.pyd" -delete
	@find koskari -type d -name "*.egg-info" -exec rm -rf {} + 2>/dev/null || true

clean-pyc:  ## Remove Python cache files
	@echo "Cleaning Python cache..."
	@find . -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
	@find . -type f -name "*.pyc" -delete
	@find . -type f -name "*.pyo" -delete

clean-native:  ## Remove native test build artifacts
	@$(MAKE) -C tests/native -f tests.mk clean

clean: clean-built clean-ext clean-pyc clean-native  ## Remove all build artifacts and caches
	@echo "Clean complete"

purge: clean  ## Deep clean (includes .tox, .eggs, etc.)
	@rm -rf .eggs .tox .mypy_cache .pytest_cache
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
	corpus-parquet-container \
	corpus-parquet-container-build \
	clean-built \
	clean-ext \
	clean-native \
	clean-pyc \
	docs \
	gdb \
	handlers \
	help \
	install \
	install-dev \
	lint \
	project \
	purge \
	python \
	regen-editors \
	report-python \
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
	typecheck \
	uninstall \
	version


# The end.
