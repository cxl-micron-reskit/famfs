
#
# The famfs build uses "outlaw subrepos". We don't like git subrepo
# functionality, so we check them out manually. The following subdirectories
# are outlaw subrepos:
#
# * ndctl         - Used to build daxctl with fsdev/famfs support
#                   (this should go away when fsdev/famfs support goes upstream)
# * libfuse       - Libfuse with famfs support
#                   (this may go away when famfs support goes upstream)
# * cmake-modules - Used for cmake support of gcov coverage testing
# * C-Thread-Pool - Thread pool library
#                   TODO: have cmake check this out (no local mods)
# * mongoose      - Used as rest server for famfs_fused
#                   TODO: have cmake check this out (no local mods)

#
# Make the build fail fast if the famfs uapi include file is not installed
#
HOSTNAME := $(shell hostname)
UID := $(shell id -u)
GID := $(shell id -g)

#
# Kernel version detection
#
KERNEL_VERSION := $(shell uname -r)
KERNEL_MAJOR := $(shell uname -r | cut -d. -f1)
KERNEL_MINOR := $(shell uname -r | cut -d. -f2)

#
# Validate kernel version (must be >= 6.x)
#
define check_kernel_version
	@if [ $(KERNEL_MAJOR) -lt 6 ]; then \
		echo "Error: famfs requires kernel version >= 6.x (detected: $(KERNEL_VERSION))"; \
		exit 1; \
	fi
endef

#
# CMake include path handling
#
# If /usr/include/linux/famfs_ioctl.h doesn't exist, we need to use the bundled
# fallback in linux_include/. Pass -U to cmake to clear any stale cached path
# that might point to a now-missing system header.
#
FAMFS_IOCTL_SYSTEM := /usr/include/linux/famfs_ioctl.h
CMAKE_CACHE_CLEAR :=
ifeq ($(wildcard $(FAMFS_IOCTL_SYSTEM)),)
    CMAKE_CACHE_CLEAR := -ULINUX_INCLUDE_BASE -UHAVE_LINUX_FAMFS_IOCTL_H
endif

#
# Determine the appropriate libfuse branch based on kernel version
# - kernel <= 6.14: use famfs-6.14
# - kernel >= 6.15: use famfs-6.MINOR (e.g., famfs-6.19 for kernel 6.19)
#
# The build will fail if the required branch does not exist in the repo.
#
LIBFUSE_BRANCH_6_14 := famfs-6.14
LIBFUSE_BRANCH_6_19 := famfs-6.19

# Default branch selection based on running kernel
# This can be overridden by passing LIBFUSE_BRANCH= on the command line
ifeq ($(shell test $(KERNEL_MAJOR) -eq 6 -a $(KERNEL_MINOR) -le 14 && echo yes),yes)
    LIBFUSE_BRANCH_AUTO := $(LIBFUSE_BRANCH_6_14)
else
    # For kernel >= 6.15, use kernel-version-specific branch
    LIBFUSE_BRANCH_AUTO := famfs-6.$(KERNEL_MINOR)
endif

# Use auto-detected branch unless explicitly overridden
LIBFUSE_BRANCH ?= $(LIBFUSE_BRANCH_AUTO)

LIBFUSE_REPO := https://github.com/jagalactic/libfuse.git

cmake-modules:
	git clone https://github.com/jagalactic/cmake-modules.git

libfuse_install:
	meson install -C $(BUILD)/libfuse

threadpool:
	@echo "Clone C-Thread-Pool"
	@if [ ! -d "C-Thread-Pool" ]; then \
		echo "cloning C-Thread-Pool..."; \
		git clone -b master https://github.com/jagalactic/C-Thread-Pool.git; \
	fi

mongoose:
	git clone -b famfs-7.19 https://github.com/jagalactic/mongoose.git

NDCTL_REPO := https://github.com/jagalactic/ndctl.git
NDCTL_BRANCH := famfs

#
# ndctl target: clone repo (if needed) and build
#
# On fresh clone, checks out the 'famfs' branch.
# If directory already exists, does not change checkout but builds if necessary.
#
ndctl:
	@if [ ! -d "ndctl" ]; then \
		echo "Cloning ndctl..."; \
		git clone -b $(NDCTL_BRANCH) $(NDCTL_REPO); \
	fi
	@if [ ! -d "ndctl/build" ]; then \
		echo "Setting up ndctl build..."; \
		meson setup ndctl/build ndctl; \
	fi
	@meson compile -C ndctl/build

#
# libfuse target: clone repo (if needed) and checkout the appropriate branch
#
# The branch is determined by:
# 1. LIBFUSE_BRANCH if explicitly set on command line
# 2. Otherwise, auto-detected based on kernel version
#
# The build will fail if the branch does not exist.
# Supports both git and jj repositories.
#
# Optimization: skip checkout and build if already on the correct branch
# and the build output exists.
#
libfuse:
	@echo "Build dir: $(BDIR), libfuse branch: $(LIBFUSE_BRANCH)"
	@if [ -z "$(BDIR)" ]; then \
		echo "Error: BDIR macro empty"; \
		exit 1; \
	fi
	@if [ ! -d "libfuse" ]; then \
		echo "Cloning libfuse..."; \
		git clone $(LIBFUSE_REPO); \
	fi
	@NEED_BUILD=0; \
	cd libfuse && \
	if [ -d ".jj" ]; then \
		CURRENT_BRANCH=$$(jj log -r @ --no-graph -T 'bookmarks' 2>/dev/null | tr -d ' '); \
	else \
		CURRENT_BRANCH=$$(git rev-parse --abbrev-ref HEAD 2>/dev/null); \
	fi; \
	if [ "$$CURRENT_BRANCH" != "$(LIBFUSE_BRANCH)" ]; then \
		echo "Branch mismatch: current=$$CURRENT_BRANCH, needed=$(LIBFUSE_BRANCH)"; \
		NEED_BUILD=1; \
	elif [ ! -f "../$(BDIR)/libfuse/lib/libfuse3.so" ]; then \
		echo "libfuse build output missing"; \
		NEED_BUILD=1; \
	else \
		echo "libfuse already on branch $(LIBFUSE_BRANCH) and built - skipping"; \
	fi; \
	cd ..; \
	if [ "$$NEED_BUILD" = "1" ]; then \
		cd libfuse && \
		if [ -d ".jj" ]; then \
			echo "libfuse is a jj repo, using jj commands..."; \
			jj git fetch && \
			if ! jj edit $(LIBFUSE_BRANCH); then \
				echo "Error: libfuse branch $(LIBFUSE_BRANCH) does not exist"; \
				echo "Available branches:"; \
				jj bookmark list | grep 'famfs' | sed 's/^/  /'; \
				exit 1; \
			fi; \
		else \
			git fetch origin && \
			if ! git checkout $(LIBFUSE_BRANCH); then \
				echo "Error: libfuse branch $(LIBFUSE_BRANCH) does not exist"; \
				echo "Available branches:"; \
				git branch -r | grep 'origin/famfs' | sed 's/origin\//  /'; \
				exit 1; \
			fi && \
			git pull origin $(LIBFUSE_BRANCH) 2>/dev/null || true; \
		fi && \
		cd .. && \
		mkdir -p $(BDIR)/libfuse && \
		(meson setup -Dexamples=false $(BDIR)/libfuse ./libfuse --wipe 2>/dev/null || \
			meson setup -Dexamples=false $(BDIR)/libfuse ./libfuse) && \
		meson compile -C $(BDIR)/libfuse; \
	fi

sanitize: cmake-modules threadpool mongoose
	$(call check_kernel_version)
	mkdir -p sanitize;
	$(MAKE) libfuse BDIR="sanitize"
	cd sanitize; cmake $(CMAKE_CACHE_CLEAR) -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_FLAGS="-fsanitize=address,undefined,leak -static-libasan -g -O1" \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -static-libasan -g -O1" ..; \
	$(MAKE)


debug:	cmake-modules threadpool mongoose
	$(call check_kernel_version)
	export BDIR="debug"
	mkdir -p debug;
	$(MAKE) libfuse BDIR="debug"
	cd debug; cmake $(CMAKE_CACHE_CLEAR) -DCMAKE_BUILD_TYPE=Debug ..; $(MAKE) #VERBOSE=1

#
# The coverage target will generate a debug build in the 'coverage' subdirectoroy
# that uses gcov to measure coverage.
# Combined coverage for smoke and unit tests can be achieved as follows:
# ./run_smoke.sh
# cd debug; make famfs_unit_coverage
#
# The comand above will direct you to html files detailing the measured coverage
#
coverage:	cmake-modules threadpool mongoose
	$(call check_kernel_version)
	mkdir -p coverage;
	$(MAKE) libfuse BDIR="coverage"
	cd coverage; cmake $(CMAKE_CACHE_CLEAR) -DCMAKE_BUILD_TYPE=Debug -DFAMFS_TEST_COVERAGE="yes" ..; $(MAKE)

smoke_coverage_fuse:
	script -e -c "./run_smoke.sh --coverage --fuse --log" \
			-O "smoke_coverage.fuse.$(HOSTNAME).log"

smoke_coverage_fuse_pcq:
	script -e -c "./run_smoke.sh --coverage --fuse --with-pcq" \
			-O "smoke_coverage.fuse.$(HOSTNAME).log"

smoke_coverage_nofuse:
	script -e -c "./run_smoke.sh --coverage --nofuse" \
			-O "smoke_coverage.nofuse.$(HOSTNAME).log"

smoke_coverage_nofuse_pcq:
	script -e -c "./run_smoke.sh --coverage --nofuse --with-pcq" \
			-O "smoke_coverage.nofuse.$(HOSTNAME).log"

unit_coverage:
	cd coverage; script -e -c "sudo make famfs_unit_coverage" \
			-O ../unit_coverage.log

# Run the coverage tests
coverage_test:	coverage
	$(MAKE) smoke_coverage_fuse
	$(MAKE) smoke_coverage_nofuse
	sudo chown -R "$(UID):$(GID)" coverage
	$(MAKE) unit_coverage

coverage_fuse:	coverage
	$(MAKE) smoke_coverage_fuse
	sudo chown -R "$(UID):$(GID)" coverage
	$(MAKE) unit_coverage

coverage_fuse_pcq:	coverage
	$(MAKE) smoke_coverage_fuse_pcq
	sudo chown -R "$(UID):$(GID)" coverage
	$(MAKE) unit_coverage

coverage_nofuse: coverage
	$(MAKE) smoke_coverage_nofuse
	sudo chown -R "$(UID):$(GID)" coverage
	$(MAKE) unit_coverage

coverage_nofuse_pcq: coverage
	$(MAKE) smoke_coverage_nofuse_pcq
	sudo chown -R "$(UID):$(GID)" coverage
	$(MAKE) unit_coverage

coverage_dual:	coverage_test

coverage_dual_pcq:	coverage
	$(MAKE) smoke_coverage_fuse_pcq
	$(MAKE) smoke_coverage_nofuse_pcq
	sudo chown -R "$(UID):$(GID)" coverage
	$(MAKE) unit_coverage

release:	cmake-modules threadpool mongoose
	$(call check_kernel_version)
	mkdir -p release;
	$(MAKE) libfuse BDIR="release"
	cd release; cmake $(CMAKE_CACHE_CLEAR) ..; $(MAKE)

#
# Default target: build with auto-detected libfuse branch based on kernel version
#
all:	ndctl debug

#
# Explicit kernel version targets: build with specific libfuse branch
# regardless of the running kernel version
#
all-6.14:	ndctl
	@echo "Building with libfuse branch $(LIBFUSE_BRANCH_6_14) (kernel 6.14 and earlier)"
	$(MAKE) debug LIBFUSE_BRANCH="$(LIBFUSE_BRANCH_6_14)"

all-6.19:	ndctl
	@echo "Building with libfuse branch $(LIBFUSE_BRANCH_6_19) (kernel 6.19)"
	$(MAKE) debug LIBFUSE_BRANCH="$(LIBFUSE_BRANCH_6_19)"

clean:
	sudo rm -rf debug release coverage sanitize

install:
	cd debug; sudo $(MAKE) install
	$(MAKE) libfuse_install BDIR="debug"

# Run the unit tests
test:
	sudo rm -rf /tmp/famfs
	cd debug; sudo ctest --output-on-failure

# Run C-Thread-Pool unit tests
threadpool-test:
	cd C-Thread-Pool/tests && ./normal_compile.sh

# Baseline smoke ***

smoke_nofuse:	debug
	script -e -c "./run_smoke.sh --nofuse" -O "smoke.$(HOSTNAME).nofuse.log"

smoke_fuse:	debug
	script -e -c "./run_smoke.sh --fuse" -O "smoke.$(HOSTNAME).fuse.log"

smoke:	debug
	make smoke_nofuse
	make smoke_fuse

smoke_dual:	debug
	make smoke_nofuse
	make smoke_fuse

# Sanitize ***

smoke_sanitize_nofuse:	sanitize
	script -e -c "./run_smoke.sh --nofuse --sanitize" \
				-O "smoke.$(HOSTNAME).san.nofuse.log"
	scripts/check_sanitizer_output.sh "smoke.$(HOSTNAME).san.nofuse.log"

smoke_sanitize_fuse:	sanitize
	script -e -c "./run_smoke.sh --fuse --sanitize" \
				-O "smoke.$(HOSTNAME).san.fuse.log"
	scripts/check_sanitizer_output.sh "smoke.$(HOSTNAME).san.fuse.log"

smoke_sanitize:	sanitize
	make smoke_sanitize_nofuse
	make smoke_sanitize_fuse

smoke_sanitize_dual:	smoke_sanitize

# Valgrind ***

smoke_valgrind_nofuse:	debug
	script -e -c "./run_smoke.sh --valgrind --nofuse" \
				-O "smoke.$(HOSTNAME).vg.nofuse.log"
	scripts/check_valgrind_output.sh "smoke.$(HOSTNAME).vg.nofuse.log"

smoke_valgrind_fuse:	debug
	script -e -c "./run_smoke.sh --valgrind --fuse" \
				-O "smoke.$(HOSTNAME).vg.fuse.log"
	scripts/check_valgrind_output.sh "smoke.$(HOSTNAME).vg.fuse.log"

smoke_valgrind: debug
	valgrind --version
	script -e -c "./run_smoke.sh --valgrind --nofuse" \
				-O "smoke.$(HOSTNAME).vg.nofuse.log"
	script -e -c "./run_smoke.sh --valgrind --fuse" \
				-O "smoke.$(HOSTNAME).vg.fuse.log"
	scripts/check_valgrind_output.sh "smoke.$(HOSTNAME).vg.nofuse.log"
	scripts/check_valgrind_output.sh "smoke.$(HOSTNAME).vg.fuse.log"

smoke_valgrind_dual:	smoke_valgrind

stress_tests:	release
	 script -e -c "./run_stress_tests.sh" -O "stress.$(HOSTNAME).log"

teardown:
	pwd
	@./scripts/teardown.sh

.PHONY:	test threadpool-test smoke debug release coverage chk_include libfuse libfuse_install sanitize \
	all all-6.14 all-6.19 ndctl
