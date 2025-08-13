
#
# Make the build fail fast if the famfs uapi include file is not installed
#
HOSTNAME := $(shell hostname)
UID := $(shell id -u)
GID := $(shell id -g)

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


LIBFUSE_REPO := https://github.com/jagalactic/libfuse.git;
LIBFUSE_BRANCH := famfs-6.14

libfuse:
	echo "Build: $(BDIR)"
	@if [ -z "$(BDIR)" ]; then \
		echo "Error: BDIR macro empty"; \
		exit -1; \
	fi
	@if [ ! -d "libfuse" ]; then \
		echo "cloning libfuse..."; \
		git clone -b $(LIBFUSE_BRANCH) $(LIBFUSE_REPO) \
	fi
	mkdir -p $(BDIR)/libfuse
	meson setup -Dexamples=false $(BDIR)/libfuse ./libfuse
	meson compile -C $(BDIR)/libfuse

sanitize: cmake-modules threadpool
	mkdir -p sanitize;
	$(MAKE) libfuse BDIR="sanitize"
	cd sanitize; cmake -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_FLAGS="-fsanitize=address,undefined,leak -static-libasan -g -O1" \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -static-libasan -g -O1" ..; \
	$(MAKE)


debug:	cmake-modules threadpool
	export BDIR="debug"
	mkdir -p debug;
	$(MAKE) libfuse BDIR="debug"
	cd debug; cmake -DCMAKE_BUILD_TYPE=Debug ..; $(MAKE) #VERBOSE=1

#
# The coverage target will generate a debug build in the 'coverage' subdirectoroy
# that uses gcov to measure coverage.
# Combined coverage for smoke and unit tests can be achieved as follows:
# ./run_smoke.sh
# cd debug; make famfs_unit_coverage
#
# The comand above will direct you to html files detailing the measured coverage
#
coverage:	cmake-modules threadpool
	mkdir -p coverage;
	$(MAKE) libfuse BDIR="coverage"
	cd coverage; cmake -DCMAKE_BUILD_TYPE=Debug -DFAMFS_TEST_COVERAGE="yes" ..; $(MAKE)

smoke_coverage_fuse:
	script -e -c "./run_smoke.sh --coverage --fuse" \
			-O "smoke_coverage.fuse.$(HOSTNAME).log"

smoke_coverage_nofuse:
	script -e -c "./run_smoke.sh --coverage --nofuse" \
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

coverage_nofuse: coverage
	$(MAKE) smoke_coverage_nofuse
	sudo chown -R "$(UID):$(GID)" coverage
	$(MAKE) unit_coverage

coverage_dual:	coverage_test

release:	cmake-modules threadpool
	mkdir -p release;
	$(MAKE) libfuse BDIR="release"
	cd release; cmake ..; $(MAKE)

all:	debug

clean:
	sudo rm -rf debug release coverage sanitize

install:
	cd debug; sudo $(MAKE) install
	$(MAKE) libfuse_install BDIR="debug"

# Run the unit tests
test:
	sudo rm -rf /tmp/famfs
	cd debug; sudo ctest --output-on-failure

smoke_nofuse:	debug
	script -e -c "./run_smoke.sh --nofuse" -O "smoke.$(HOSTNAME).nofuse.log"

smoke_fuse:	debug
	script -e -c "./run_smoke.sh --fuse" -O "smoke.$(HOSTNAME).fuse.log"

# Run the smoke tests
smoke:	debug
	make smoke_nofuse
	make smoke_fuse

smoke_dual:	debug
	make smoke_nofuse
	make smoke_fuse

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

.PHONY:	test smoke debug release coverage chk_include libfuse libfuse_install sanitize
