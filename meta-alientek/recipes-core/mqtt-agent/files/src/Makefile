CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
LDFLAGS ?=
LDLIBS ?=
OTA_MQTT_BACKEND ?= paho

MQTT_CPPFLAGS :=
ifeq ($(OTA_MQTT_BACKEND),paho)
MQTT_CPPFLAGS := -DOTA_MQTT_BACKEND_PAHO
LDLIBS += -lpaho-mqtt3c
else
MQTT_CPPFLAGS := -DOTA_MQTT_BACKEND_STUB
endif

SOURCES = mqtt-agent.cpp ota-mqtt.cpp ota-state.cpp
HEADERS = ota-mqtt.hpp ota-state.hpp

mqtt-agent: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(MQTT_CPPFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)

.PHONY: clean
clean:
	rm -f mqtt-agent
