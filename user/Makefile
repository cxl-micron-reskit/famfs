
debug:
	mkdir -p debug; cp testlib/.checkfiles.yml $@
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
coverage:
	mkdir -p coverage; cp testlib/.checkfiles.yml $@
	cd coverage; cmake -DCMAKE_BUILD_TYPE=Debug -DFAMFS_TEST_COVERAGE="yes" ..; make

# Run the coverage tests
coverage_test:	coverage
	-scripts/teardown.sh
	./run_smoke.sh --coverage
	cd coverage; sudo make famfs_unit_coverage

release:
	mkdir -p release; cp testlib/.checkfiles.yml $@
	cd release; cmake ..; make

all:	debug

clean:
	sudo rm -rf debug release coverage

install:
	cd debug; sudo make install

# Run the unit tests
test:
	sudo rm -rf /tmp/famfs
	cd debug; sudo ctest --output-on-failure

# Run the smoke tests
smoke:	debug
	-scripts/teardown.sh
	script -c ./run_smoke.sh -O smoke.log

smoke_valgrind: debug
	-scripts/teardown.sh
	script -c "./run_smoke.sh --valgrind" -O smoke.log
	scripts/check_valgrind_output.sh smoke.log

teardown:
	pwd
	@./scripts/teardown.sh

.PHONY:	test smoke debug release coverage
