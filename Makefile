
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
# Error tests (smoke/test_errors.sh, via run_smoke.sh) are skipped by default
# so that interactive "make smoke*" runs stay quick. CI enables them by passing
# WITH_ERRORS=1 on the make command line, which appends --with-errors to every
# run_smoke.sh invocation below. A command-line WITH_ERRORS propagates to the
# recursive sub-makes (smoke_dual, coverage_*, smoke_sanitize/valgrind_dual)
# via MAKEFLAGS, so setting it once on the top-level target is enough.
#
ERRFLAG :=
ifdef WITH_ERRORS
    ERRFLAG := --with-errors
endif

#
# libfuse branch selection
#
# famfs_fused needs a libfuse with famfs support, and the checkout in
# ./libfuse decides which fuse ABI famfs_fused is built for (see
# src/famfs_fuse_abi.h):
#
# - If ./libfuse exists it is built as is - never switched (see the libfuse
#   target); the developer owns that checkout.
# - Otherwise it is cloned from LIBFUSE_REPO at branch LIBFUSE_BRANCH. If
#   LIBFUSE_BRANCH is not given (environment or command line), it is chosen
#   from the running kernel, in this order:
#     famfs.extmap - the kernel speaks the extent-map fuse ABI
#                    (Miklos Szeredi's libfuse work)
#     famfs-6.14   - DAX_FMAP ABI, kernels <= 6.14
#     famfs        - DAX_FMAP ABI, kernels >= 6.15 (also the fallback when
#                    the kernel has no famfs fuse support at all)
#   The CI jobs rm -rf libfuse before every build, so this choice is what
#   decides each agent's fuse ABI; an agent with an extent-map kernel gets
#   famfs.extmap with no per-agent configuration.
#
# The libfuse target also compares the ABI the checkout's headers select
# against the one the running kernel speaks and warns on a mismatch. It is
# only a warning: the build host need not be the test host.
#
LIBFUSE_BRANCH_6_14   := famfs-6.14
LIBFUSE_BRANCH_6_19   := famfs
LIBFUSE_BRANCH_EXTMAP := famfs.extmap

LIBFUSE_REPO := https://github.com/jagalactic/libfuse.git

# Fuse DAX ABI of the running kernel: extmap, daxfmap or none. The kallsyms
# marker symbols are the same ones famfs_get_kernel_fuse_abi() in
# src/famfs_misc.c keys on, but they only appear once the fuse module is
# loaded, so the installed uapi fuse.h is a second signal (the CI VMs install
# headers from the kernel they boot). FUSE_UAPI_H is overridable for tests.
FUSE_UAPI_H ?= /usr/include/linux/fuse.h
KERNEL_FUSE_ABI := $(shell \
	if grep -qsw fuse_backing_is_dax /proc/kallsyms; then echo extmap; \
	elif grep -qsw famfs_fuse_iomap_begin /proc/kallsyms; then echo daxfmap; \
	elif grep -qsw FUSE_NOTIFY_BACKING_MAP $(FUSE_UAPI_H); then echo extmap; \
	elif grep -qsw FUSE_DAX_FMAP $(FUSE_UAPI_H); then echo daxfmap; \
	else echo none; fi)

ifeq ($(KERNEL_FUSE_ABI),extmap)
    LIBFUSE_BRANCH_AUTO := $(LIBFUSE_BRANCH_EXTMAP)
    LIBFUSE_BRANCH_WHY  := kernel speaks the extent-map fuse ABI
else ifeq ($(shell test $(KERNEL_MAJOR) -eq 6 -a $(KERNEL_MINOR) -le 14 && echo yes),yes)
    LIBFUSE_BRANCH_AUTO := $(LIBFUSE_BRANCH_6_14)
    LIBFUSE_BRANCH_WHY  := kernel $(KERNEL_VERSION) is 6.14 or older
else
    LIBFUSE_BRANCH_AUTO := $(LIBFUSE_BRANCH_6_19)
    LIBFUSE_BRANCH_WHY  := kernel $(KERNEL_VERSION) is 6.15 or newer, fuse ABI $(KERNEL_FUSE_ABI)
endif

# Environment or command-line LIBFUSE_BRANCH wins (?= does not override it)
ifdef LIBFUSE_BRANCH
    LIBFUSE_BRANCH_WHY := LIBFUSE_BRANCH given explicitly
endif
LIBFUSE_BRANCH ?= $(LIBFUSE_BRANCH_AUTO)

#
# Default target
#
all:	ndctl
	@echo "Building with libfuse branch $(LIBFUSE_BRANCH) ($(LIBFUSE_BRANCH_WHY))"
	$(MAKE) debug

cmake-modules:
	@if [ ! -d "cmake-modules" ]; then \
		echo "cloning cmake-modules..."; \
		git clone https://github.com/jagalactic/cmake-modules.git; \
	fi

libfuse_install:
	meson install -C $(BDIR)/libfuse

threadpool:
	@echo "Clone C-Thread-Pool"
	@if [ ! -d "C-Thread-Pool" ]; then \
		echo "cloning C-Thread-Pool..."; \
		git clone -b master https://github.com/jagalactic/C-Thread-Pool.git; \
	fi

mongoose:
	@if [ ! -d "mongoose" ]; then \
		echo "cloning mongoose..."; \
		git clone -b famfs-7.19 https://github.com/jagalactic/mongoose.git; \
	fi

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
		meson setup --default-library=both ndctl/build ndctl; \
	elif [ ! -f "ndctl/build/daxctl/lib/libdaxctl.a" ]; then \
		echo "Reconfiguring ndctl build for static libraries..."; \
		meson setup --reconfigure --default-library=both ndctl/build ndctl; \
	fi
	@meson compile -C ndctl/build

#
# libfuse target: clone repo (if needed) and checkout the appropriate branch
#
# The branch is LIBFUSE_BRANCH (command line, environment, or the default;
# see "libfuse branch selection" above) and only matters on a fresh clone.
#
# The build will fail if the branch does not exist.
# Supports both git and jj repositories.
#
# Optimization: skip checkout and build if already on the correct branch
# and the build output exists.
#
#
# libfuse: build, don't branch-manage.
#
# Clone libfuse on first use and check out the kernel-appropriate branch only
# on that fresh clone (zero local state, so the checkout is always safe). We
# deliberately do NOT switch branches on an existing checkout - the developer
# owns the libfuse branch, like the other manually-managed "outlaw" subrepos.
# This keeps the build VCS-agnostic (no jj/git branch dance) and avoids the
# failure where jj treats a pushed branch as immutable. ninja makes the rebuild
# incremental, so we just compile whatever is checked out.
#
libfuse:
	@echo "Build dir: $(BDIR), libfuse branch: $(LIBFUSE_BRANCH) ($(LIBFUSE_BRANCH_WHY))"
	@if [ -z "$(BDIR)" ]; then \
		echo "Error: BDIR macro empty"; \
		exit 1; \
	fi
	@if [ ! -d "libfuse" ]; then \
		echo "Cloning libfuse at branch $(LIBFUSE_BRANCH)..."; \
		git clone $(LIBFUSE_REPO) || { echo "Error: git clone failed"; exit 1; }; \
		(cd libfuse && git checkout $(LIBFUSE_BRANCH)) || { \
			echo "Error: libfuse branch '$(LIBFUSE_BRANCH)' not found"; exit 1; }; \
	else \
		echo "Using the existing ./libfuse checkout as is" \
		     "($$(cd libfuse && git rev-parse --abbrev-ref HEAD 2>/dev/null)," \
		     "$$(cd libfuse && git rev-parse --short HEAD 2>/dev/null))"; \
	fi
	@if grep -qs 'define FUSE_EXT_MAP_CREATE' libfuse/include/*.h; then libabi=extmap; \
	elif grep -qs 'define FUSE_CAP_DAX_FMAP' libfuse/include/*.h; then libabi=daxfmap; \
	else libabi=unknown; fi; \
	kabi="$(KERNEL_FUSE_ABI)"; \
	echo "libfuse fuse ABI: $$libabi, running kernel fuse ABI: $$kabi"; \
	if [ "$$kabi" != none ] && [ "$$libabi" != unknown ] && [ "$$kabi" != "$$libabi" ]; then \
		echo "WARNING: ./libfuse selects the '$$libabi' fuse ABI but this kernel speaks '$$kabi';"; \
		echo "         famfs_fused built from it will refuse to mount on this kernel."; \
		echo "         To fix: rm -rf libfuse and rebuild (the branch is then chosen for"; \
		echo "         this kernel), or check out the right branch in ./libfuse yourself"; \
		echo "         (extmap: $(LIBFUSE_BRANCH_EXTMAP); daxfmap: $(LIBFUSE_BRANCH_6_19) or $(LIBFUSE_BRANCH_6_14))"; \
	fi
	@mkdir -p $(BDIR)/libfuse
	@if [ ! -f "$(BDIR)/libfuse/build.ninja" ]; then \
		meson setup -Dexamples=false $(BDIR)/libfuse ./libfuse; \
	fi
	@meson compile -C $(BDIR)/libfuse

sanitize: cmake-modules threadpool mongoose ndctl
	$(call check_kernel_version)
	mkdir -p sanitize;
	$(MAKE) libfuse BDIR="sanitize"
	cd sanitize; cmake $(CMAKE_CACHE_CLEAR) -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_FLAGS="-fsanitize=address,undefined,leak -static-libasan -g -O1" \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -static-libasan -g -O1" ..; \
	$(MAKE)


debug:	cmake-modules threadpool mongoose ndctl
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
coverage:	cmake-modules threadpool mongoose ndctl
	$(call check_kernel_version)
	mkdir -p coverage;
	$(MAKE) libfuse BDIR="coverage"
	cd coverage; cmake $(CMAKE_CACHE_CLEAR) -DCMAKE_BUILD_TYPE=Debug -DFAMFS_TEST_COVERAGE="yes" ..; $(MAKE)

smoke_coverage_fuse:
	script -e -c "./run_smoke.sh --coverage --fuse --log $(ERRFLAG)" \
			-O "smoke_coverage.fuse.$(HOSTNAME).log"

smoke_coverage_fuse_pcq:
	script -e -c "./run_smoke.sh --coverage --fuse --with-pcq $(ERRFLAG)" \
			-O "smoke_coverage.fuse.$(HOSTNAME).log"

smoke_coverage_nofuse:
	script -e -c "./run_smoke.sh --coverage --nofuse $(ERRFLAG)" \
			-O "smoke_coverage.nofuse.$(HOSTNAME).log"

smoke_coverage_nofuse_pcq:
	script -e -c "./run_smoke.sh --coverage --nofuse --with-pcq $(ERRFLAG)" \
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

release:	cmake-modules threadpool mongoose ndctl
	$(call check_kernel_version)
	mkdir -p release;
	$(MAKE) libfuse BDIR="release"
	cd release; cmake $(CMAKE_CACHE_CLEAR) ..; $(MAKE)


#
# Explicit libfuse-branch targets (only matter when ./libfuse is absent and
# gets cloned)
#
all-old:	ndctl
	@echo "Building with libfuse branch $(LIBFUSE_BRANCH_6_14) (kernel <= 6.14)"
	$(MAKE) debug LIBFUSE_BRANCH="$(LIBFUSE_BRANCH_6_14)"

all-6.19:	ndctl
	@echo "Building with libfuse branch $(LIBFUSE_BRANCH_6_19) (DAX_FMAP ABI, kernel >= 6.15)"
	$(MAKE) debug LIBFUSE_BRANCH="$(LIBFUSE_BRANCH_6_19)"

all-new:	ndctl
	@echo "Building with libfuse branch $(LIBFUSE_BRANCH_EXTMAP) (extent-map ABI)"
	$(MAKE) debug LIBFUSE_BRANCH="$(LIBFUSE_BRANCH_EXTMAP)"

clean:
	sudo rm -rf debug release coverage sanitize
	rm -rf ndctl/build
	rm -rf libfuse/build

install:
	sudo cmake --install debug
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
	script -e -c "./run_smoke.sh --nofuse $(ERRFLAG)" -O "smoke.$(HOSTNAME).nofuse.log"

smoke_fuse:	debug
	script -e -c "./run_smoke.sh --fuse $(ERRFLAG)" -O "smoke.$(HOSTNAME).fuse.log"

smoke:	debug
	make smoke_nofuse
	make smoke_fuse

smoke_dual:	debug
	make smoke_nofuse
	make smoke_fuse

# Sanitize ***

smoke_sanitize_nofuse:	sanitize
	script -e -c "./run_smoke.sh --nofuse --sanitize $(ERRFLAG)" \
				-O "smoke.$(HOSTNAME).san.nofuse.log"
	scripts/check_sanitizer_output.sh "smoke.$(HOSTNAME).san.nofuse.log"

smoke_sanitize_fuse:	sanitize
	script -e -c "./run_smoke.sh --fuse --sanitize $(ERRFLAG)" \
				-O "smoke.$(HOSTNAME).san.fuse.log"
	scripts/check_sanitizer_output.sh "smoke.$(HOSTNAME).san.fuse.log"

smoke_sanitize:	sanitize
	make smoke_sanitize_nofuse
	make smoke_sanitize_fuse

smoke_sanitize_dual:	smoke_sanitize

# Valgrind ***

smoke_valgrind_nofuse:	debug
	script -e -c "./run_smoke.sh --valgrind --nofuse $(ERRFLAG)" \
				-O "smoke.$(HOSTNAME).vg.nofuse.log"
	scripts/check_valgrind_output.sh "smoke.$(HOSTNAME).vg.nofuse.log"

smoke_valgrind_fuse:	debug
	script -e -c "./run_smoke.sh --valgrind --fuse $(ERRFLAG)" \
				-O "smoke.$(HOSTNAME).vg.fuse.log"
	scripts/check_valgrind_output.sh "smoke.$(HOSTNAME).vg.fuse.log"

smoke_valgrind: debug
	valgrind --version
	script -e -c "./run_smoke.sh --valgrind --nofuse $(ERRFLAG)" \
				-O "smoke.$(HOSTNAME).vg.nofuse.log"
	script -e -c "./run_smoke.sh --valgrind --fuse $(ERRFLAG)" \
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
	all all-old all-6.19 all-new ndctl
