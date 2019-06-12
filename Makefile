SRC	:= main.cpp
TRG	:= mkpkg

$(TRG): $(SRC)
	g++ -O3 -lboost_system -lboost_filesystem -o $@ $^
