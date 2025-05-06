
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


libfuse:
	echo "Build: $(BDIR)"
	@if [ -z "$(BDIR)" ]; then \
		echo "Error: BDIR macro empty"; \
		exit -1; \
	fi
	@if [ ! -d "libfuse" ]; then \
		echo "cloning libfuse..."; \
		git clone -b famfs https://github.com/jagalactic/libfuse.git; \
	fi
	mkdir -p $(BDIR)/libfuse
	meson setup -Dexamples=false $(BDIR)/libfuse ./libfuse
	meson compile -C $(BDIR)/libfuse

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

# Run the coverage tests
coverage_test:	coverage
	script -c "./run_smoke.sh --coverage --nofuse" \
			-O "smoke_coverage.nofuse.$(HOSTNAME).log"
	script -c "./run_smoke.sh --coverage --fuse" \
			-O "smoke_coverage.fuse.$(HOSTNAME).log"
	sudo chown -R "$(UID):$(GID)" coverage
	cd coverage; script -e -c "sudo make famfs_unit_coverage" -O ../unit_coverage.log

release:	cmake-modules threadpool
	mkdir -p release;
	$(MAKE) libfuse BDIR="release"
	cd release; cmake ..; $(MAKE)

all:	debug

clean:
	sudo rm -rf debug release coverage

install:
	cd debug; sudo $(MAKE) install
	$(MAKE) libfuse_install BDIR="debug"

# Run the unit tests
test:
	sudo rm -rf /tmp/famfs
	cd debug; sudo ctest --output-on-failure

# Run the smoke tests
smoke:	debug
	script -e -c "./run_smoke.sh --nofuse" -O "smoke.$(HOSTNAME).nofuse.log"
	script -e -c "./run_smoke.sh --fuse" -O "smoke.$(HOSTNAME).fuse.log"

smoke_valgrind: debug
	valgrind --version
	script -e -c "./run_smoke.sh --valgrind --nofuse" \
				-O "smoke.$(HOSTNAME).vg.nofuse.log"
	script -e -c "./run_smoke.sh --valgrind --fuse" \
				-O "smoke.$(HOSTNAME).vg.fuse.log"
	scripts/check_valgrind_output.sh "smoke.$(HOSTNAME).vg.nofuse.log"
	scripts/check_valgrind_output.sh "smoke.$(HOSTNAME).vg.fuse.log"

stress_tests:	release
	 script -e -c "./run_stress_tests.sh" -O "stress.$(HOSTNAME).log"

teardown:
	pwd
	@./scripts/teardown.sh

.PHONY:	test smoke debug release coverage chk_include libfuse libfuse_install
