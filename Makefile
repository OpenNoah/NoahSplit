.PHONY: all
all: mkpkg pkginfo xor pkginfo.mipsel pkginfo-static.mipsel

mkpkg: main.cpp np1000.cpp np890.cpp
	g++ -O3 -lboost_system -lboost_filesystem -lz -o $@ $^

pkginfo: info.c
	gcc -O3 -o $@ $^

%: %.cpp
	g++ -O3 -o $@ $^

pkginfo.mipsel: info.c
	mipsel-linux-gcc -s --std=gnu99 -Os -o $@ $^ -fdata-sections -ffunction-sections -fno-omit-frame-pointer -Wl,--gc-sections

pkginfo-static.mipsel: info.c
	mipsel-linux-gcc -s --static --std=gnu99 -Os -o $@ $^ -fdata-sections -ffunction-sections -fno-omit-frame-pointer -Wl,--gc-sections

.PHONY: clean
clean:
	rm -f mkpkg pkginfo pkginfo.mipsel pkginfo-static.mipsel
