CXX = clang++

EXE = imgui.exe
IMGUI_DIR = ../..
MAIN_DIR = ../../../..

MAIN_SOURCES = $(wildcard $(MAIN_DIR)/framework/*.cpp)
ELEMENTS_SOURCES = $(wildcard $(MAIN_DIR)/framework/widgets/*.cpp)
HELPERS_SOURCES = $(wildcard $(MAIN_DIR)/framework/helpers/*.cpp)

SOURCES = main.cpp
SOURCES += $(IMGUI_DIR)/imgui.cpp
SOURCES += $(IMGUI_DIR)/imgui_draw.cpp
SOURCES += $(IMGUI_DIR)/imgui_tables.cpp
SOURCES += $(IMGUI_DIR)/imgui_widgets.cpp
SOURCES += $(IMGUI_DIR)/imgui_freetype.cpp
SOURCES += $(IMGUI_DIR)/backends/imgui_impl_win32.cpp
SOURCES += $(IMGUI_DIR)/backends/imgui_impl_dx11.cpp

SOURCES += $(MAIN_SOURCES)
SOURCES += $(ELEMENTS_SOURCES)
SOURCES += $(HELPERS_SOURCES)

OBJS = $(addsuffix .o, $(basename $(notdir $(SOURCES))))
DEPS = $(OBJS:.o=.d)

UNAME_S := $(shell uname -s)

CXXFLAGS = -std=c++20
CXXFLAGS += -I$(IMGUI_DIR)
CXXFLAGS += -I$(IMGUI_DIR)/backends
CXXFLAGS += -I$(MAIN_DIR)/framework
CXXFLAGS += -I$(MAIN_DIR)/framework/widgets
CXXFLAGS += -I$(MAIN_DIR)/framework/helpers
CXXFLAGS += -I$(MAIN_DIR)/thirdparty/freetype/include
CXXFLAGS += -I$(MAIN_DIR)/thirdparty/dxsdk/include
CXXFLAGS += -I$(MAIN_DIR)/thirdparty/imgui
CXXFLAGS += -g
# CXXFLAGS += -Wall          # Отключаем все предупреждения
# CXXFLAGS += -Wformat       # Отключаем конкретное предупреждение
CXXFLAGS += -w 

FREETYPE_LIB_PATH = $(MAIN_DIR)/thirdparty/freetype/win64
DXSDK_LIB_PATH = $(MAIN_DIR)/thirdparty/dxsdk/lib/x64

LIBS += -L$(FREETYPE_LIB_PATH) -lfreetype
LIBS += -L$(DXSDK_LIB_PATH)
LIBS += -ld3d11 -ld3dcompiler -ldxgi -ldxguid
LIBS += -luser32 -lgdi32 -limm32 -lole32 -loleaut32 -luuid -lversion -lwinmm

ifeq ($(UNAME_S), Linux)
    $(error DirectX 11 is only supported on Windows)
endif

ifeq ($(UNAME_S), Darwin)
    $(error DirectX 11 is only supported on Windows)
endif

ifeq ($(OS), Windows_NT)
    CXXFLAGS += -DUNICODE -D_UNICODE
    CFLAGS = $(CXXFLAGS)
    
    ifeq (,$(wildcard $(FREETYPE_LIB_PATH)/freetype.lib))
        $(error freetype.lib not found in $(FREETYPE_LIB_PATH). Please build freetype first)
    endif
endif

%.d: %.cpp
	$(CXX) $(CXXFLAGS) -MM $< > $@

%.d: $(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -MM $< > $@

%.d: $(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -MM $< > $@

%.d: $(MAIN_DIR)/framework/%.cpp
	$(CXX) $(CXXFLAGS) -MM $< > $@

%.d: $(MAIN_DIR)/framework/widgets/%.cpp
	$(CXX) $(CXXFLAGS) -MM $< > $@

%.d: $(MAIN_DIR)/framework/helpers/%.cpp
	$(CXX) $(CXXFLAGS) -MM $< > $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: $(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: $(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: $(MAIN_DIR)/framework/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: $(MAIN_DIR)/framework/widgets/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: $(MAIN_DIR)/framework/helpers/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

all: $(EXE)
	@./$(EXE)

$(EXE): $(OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS)
	@echo Build complete for $(ECHO_MESSAGE)

-include $(DEPS)

clean:
	rm -f $(EXE) $(OBJS) $(DEPS)