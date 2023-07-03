
all:
	cd tagfs; make all
	mkdir -p test/debug
	cd test/debug; cmake -DCMAKE_BUILD_TYPE=Debug ..; make
	mkdir -p test/release
	cd test/release; cmake ..; make

clean:
	cd tagfs; make clean
	rm -rf test/debug test/release

