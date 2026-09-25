CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
LDFLAGS ?=
LDLIBS ?=
OTA_MQTT_BACKEND ?= paho
# 宿主机：从 ota-agent 拉共享源；Yocto do_configure 会拷到本目录
OTA_SHARED ?= ../../../ota-agent/files/src

MQTT_CPPFLAGS :=
ifeq ($(OTA_MQTT_BACKEND),paho)
MQTT_CPPFLAGS := -DOTA_MQTT_BACKEND_PAHO
LDLIBS += -lpaho-mqtt3c
else
MQTT_CPPFLAGS := -DOTA_MQTT_BACKEND_STUB
endif

ifneq ($(wildcard ota-defaults.cpp),)
SHARED_SRCS := ota-mqtt.cpp ota-state.cpp ota-defaults.cpp
else
SHARED_SRCS := $(OTA_SHARED)/ota-mqtt.cpp $(OTA_SHARED)/ota-state.cpp $(OTA_SHARED)/ota-defaults.cpp
CXXFLAGS += -I$(OTA_SHARED)
endif

SOURCES = mqtt-agent.cpp $(SHARED_SRCS)

mqtt-agent: $(SOURCES)
	$(CXX) $(CXXFLAGS) $(MQTT_CPPFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)

.PHONY: clean
clean:
	rm -f mqtt-agent
