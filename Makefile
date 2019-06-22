.PHONY: all
all: mkpkg pkginfo pkginfo.mipsel

mkpkg: main.cpp
	g++ -O3 -lboost_system -lboost_filesystem -o $@ $^

pkginfo: info.c
	gcc -O3 -o $@ $^

pkginfo.mipsel: info.c
	mipsel-linux-gcc -s -static -Os -o $@ $^ -fdata-sections -ffunction-sections -fno-omit-frame-pointer -Wl,--gc-sections
