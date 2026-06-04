CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2
ifeq ($(OS),Windows_NT)
EXEEXT ?= .exe
else
EXEEXT ?=
endif
TARGET := traffic_signal$(EXEEXT)
SRC := src/main.cpp

.PHONY: all run smoke-test clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f traffic_signal traffic_signal.exe traffic_signal_simulator.zip
	rm -rf build
