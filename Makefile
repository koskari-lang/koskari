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

build: clean-built report-python  ## Build wheel package using uv via tox
	@$(call checkfor,tox)
	@echo "Building $(PROJECT) $(VERSION)..."
	@tox -qe build
	@echo "Build complete: $(DIST_DIR)/$(PROJECT)-$(VERSION)-*.whl"

build-ext: report-python  ## Build native extensions only
	@echo "Building native extensions (DEBUG=$(DEBUG))..."
	@DEBUG=$(DEBUG) $(PYTHON) setup.py build_ext --inplace

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

test:  ## Run tests on current Python version via tox
	@$(call checkfor,tox)
	@echo "Running tests..."
	@tox -qe py

test-all:  ## Run tests on all supported Python versions via tox
	@$(call checkfor,tox)
	@echo "Running tests on all Python versions..."
	@tox -qe py39,py310,py311,py312

tox:  ## Run full tox suite (tests + lint + typecheck)
	@$(call checkfor,tox)
	@echo "Running full tox suite..."
	@tox -q


##@ Container-based Memory Testing

CONTAINER_IMAGE := koskari-native-tests
CONTAINER_ENGINE := $(shell which podman 2>/dev/null || which docker 2>/dev/null || echo "podman")

test-container-build:  ## Build the native test container
	@echo "Building container image $(CONTAINER_IMAGE)..."
	$(CONTAINER_ENGINE) build -t $(CONTAINER_IMAGE) -f tests/native/Containerfile tests/native/

test-native: test-container-build  ## Run all native tests in container
	@echo "Running all native memory tests in container..."
	$(CONTAINER_ENGINE) run --rm -v $(PWD):/src:Z $(CONTAINER_IMAGE) all

test-asan: test-container-build  ## Run ASan tests in container
	@echo "Running ASan tests in container..."
	$(CONTAINER_ENGINE) run --rm -v $(PWD):/src:Z $(CONTAINER_IMAGE) asan

test-valgrind: test-container-build  ## Run Valgrind tests in container
	@echo "Running Valgrind tests in container..."
	$(CONTAINER_ENGINE) run --rm -v $(PWD):/src:Z $(CONTAINER_IMAGE) valgrind


##@ Code Quality

lint:  ## Run linting (flake8) via tox
	@$(call checkfor,tox)
	@echo "Running flake8..."
	@tox -qe flake8

typecheck:  ## Run type checking (mypy) via tox
	@$(call checkfor,tox)
	@echo "Running mypy..."
	@tox -qe mypy


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

.PHONY: help report-python build build-ext install install-dev uninstall test test-all tox test-native test-native-asan test-native-valgrind clean-native container-build container-test container-test-asan container-test-valgrind lint typecheck format clean-built clean-ext clean-pyc clean purge project version python


# The end.
