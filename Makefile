RACK_DIR ?= ../..

SOURCES += $(wildcard src/*.cpp src/*/*.cpp)
DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/plugin.mk

# After plugin.mk so it overrides the SDK's -std=c++11.
CXXFLAGS += -std=c++17
