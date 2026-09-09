CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
LDFLAGS ?=

SOURCES = ota-agent.cpp ota-state.cpp ota-download.cpp ota-exec.cpp ota-mqtt.cpp
HEADERS = ota-agent.hpp ota-state.hpp ota-download.hpp ota-exec.hpp ota-mqtt.hpp

ota-agent: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(SOURCES)

.PHONY: clean

clean:
	rm -f ota-agent
