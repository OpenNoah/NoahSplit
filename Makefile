.PHONY: all
all: mkpkg pkginfo pkginfo.mipsel

mkpkg: main.cpp
	g++ -O3 -lboost_system -lboost_filesystem -o $@ $^

pkginfo: info.cpp
	g++ -O3 -o $@ $^

pkginfo.mipsel: info.cpp
	mipsel-linux-g++ -s -static -Os -o $@ $^
