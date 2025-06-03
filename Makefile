ifndef BUILD_LINUX
ifndef BUILD_WINDOWS
$(error expected either BUILD_LINUX or BUILD_WINDOWS)
endif
endif

ifdef BUILD_LINUX
install_path=/usr/local/include/
endif

ifdef BUILD_WINDOWS
install_path=/usr/x86_64-w64-mingw32/usr/include/
endif

install:
	cp terse.hh $(install_path)/terse.hh

test:
	g++ -std=c++23 test.cc -o test.out -g
