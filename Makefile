
#
# Make the build fail fast if the famfs uapi include file is not installed
#
chk_include:
	scripts/chk_include.sh

cmake-modules:
	git clone https://github.com/jagalactic/cmake-modules.git

debug:	cmake-modules chk_include
	mkdir -p debug;
	cd debug; cmake -DCMAKE_BUILD_TYPE=Debug ..; make #VERBOSE=1

#
# The coverage target will generate a debug build in the 'coverage' subdirectoroy
# that uses gcov to measure coverage.
# Combined coverage for smoke and unit tests can be achieved as follows:
# ./run_smoke.sh
# cd debug; make famfs_unit_coverage
#
# The comand above will direct you to html files detailing the measured coverage
#
coverage:	cmake-modules
	mkdir -p coverage;
	cd coverage; cmake -DCMAKE_BUILD_TYPE=Debug -DFAMFS_TEST_COVERAGE="yes" ..; make

# Run the coverage tests
coverage_test:	coverage
	-scripts/teardown.sh
	-scripts/install_kmod.sh
	script -c "./run_smoke.sh --coverage" -O smoke_coverage.log
	cd coverage; sudo script -c "make famfs_unit_coverage" -O ../unit_coverage.log

release:	cmake-modules chk_include
	mkdir -p release;
	cd release; cmake ..; make

all:	debug

clean:
	sudo rm -rf debug release coverage

install:
	cd debug; sudo make install
	-scripts/install_kmod.sh

# Run the unit tests
test:
	sudo rm -rf /tmp/famfs
	cd debug; sudo ctest --output-on-failure

# Run the smoke tests
smoke:	debug
	-scripts/install_kmod.sh
	-scripts/teardown.sh
	script -c ./run_smoke.sh -O smoke.log

smoke_valgrind: debug
	-scripts/teardown.sh
	valgrind --version
	script -c "./run_smoke.sh --valgrind" -O smoke.log
	scripts/check_valgrind_output.sh smoke.log

stress_tests:	release
	 script -c "./run_stress_tests.sh" -O stress.log

teardown:
	pwd
	@./scripts/teardown.sh

.PHONY:	test smoke debug release coverage chk_include
