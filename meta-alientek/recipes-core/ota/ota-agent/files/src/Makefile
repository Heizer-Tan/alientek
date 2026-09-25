CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
LDFLAGS ?=
LDLIBS ?=

# 默认 stub：宿主机无 paho 亦可编译；Yocto 配方设 OTA_MQTT_BACKEND=paho
OTA_MQTT_BACKEND ?= stub

# 独立变量，避免 bitbake/命令行覆盖 CXXFLAGS 时丢掉后端宏
MQTT_CPPFLAGS :=
ifeq ($(OTA_MQTT_BACKEND),paho)
MQTT_CPPFLAGS := -DOTA_MQTT_BACKEND_PAHO
LDLIBS += -lpaho-mqtt3c
else
MQTT_CPPFLAGS := -DOTA_MQTT_BACKEND_STUB
endif

SOURCES = ota-agent.cpp ota-state.cpp ota-download.cpp ota-exec.cpp ota-mqtt.cpp ota-defaults.cpp
HEADERS = ota-agent.hpp ota-state.hpp ota-download.hpp ota-exec.hpp ota-mqtt.hpp ota-defaults.hpp

ota-agent: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(MQTT_CPPFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)

.PHONY: clean

clean:
	rm -f ota-agent
