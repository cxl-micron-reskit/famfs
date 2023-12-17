
debug:
	mkdir -p debug
	cd debug; cmake -DCMAKE_BUILD_TYPE=Debug ..; make 
#	cd debug; cmake -DCMAKE_BUILD_TYPE=Debug ..; make VERBOSE=1

release:
	mkdir -p release
	cd release; cmake ..

all:	debug

clean:
	rm -rf debug release

install:
	cd debug; sudo make install

test:
	sudo rm -rf /tmp/famfs
	cd debug; sudo ctest --output-on-failure

.PHONY:	test
