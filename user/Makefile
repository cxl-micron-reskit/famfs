
debug:
	mkdir -p debug
	cd debug; cmake -DCMAKE_BUILD_TYPE=Debug ..; make #VERBOSE=1

#
# The coverage target will generate a debug build that uses gcov to measure coverage.
# Combined coverage for smoke and unit tests can be achieved as follows:
# ./run_smoke.sh
# cd debug; make famfs_unit_coverage
#
# The comand above will direct you to html files detailing the measured coverage
#
coverage:
	mkdir -p debug
	cd debug; cmake -DCMAKE_BUILD_TYPE=Debug -DFAMFS_TEST_COVERAGE="yes" ..; make

release:
	mkdir -p release
	cd release; cmake ..; make

all:	debug

clean:
	sudo rm -rf debug release

install:
	cd debug; sudo make install

# Run the unit tests
test:
	sudo rm -rf /tmp/famfs
	cd debug; sudo ctest --output-on-failure

# Run the smoke tests
smoke:
	./run_smoke.sh

.PHONY:	test smoke debug release coverage
