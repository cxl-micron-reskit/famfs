
all:	FORCE
	cd kmod; make all
	mkdir -p user/debug
	cd user/debug; cmake -DCMAKE_BUILD_TYPE=Debug ..; make
#	mkdir -p test/release
#	cd test/release; cmake ..; make

clean:
	cd kmod; make clean
	-rm -rf user/debug user/release

doxygen:
	doxygen Doxyfile

checkfiles:	FORCE
	@./scripts/checkfiles

FORCE: ;
