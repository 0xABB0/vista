ifeq ($(origin CC),default)
CC=clang
endif
GLSLC?=glslc
VULKAN_SDK?=C:/VulkanSDK/1.4.321.1
BUILD?=build

CORESRC=src/vkcore.c src/render.c src/atmos.c src/scene.c src/terrain.c src/sky.c src/veg.c src/water.c src/game.c src/math.c src/assets_io.c vendor/volk.c
WINSRC=$(CORESRC) src/plat_win.c
CFLAGS=-O2 -std=c11 -Isrc -Ivendor -I$(VULKAN_SDK)/Include -D_CRT_SECURE_NO_WARNINGS -DVK_USE_PLATFORM_WIN32_KHR -Wall -Wno-unused-function
WINLIBS=-luser32 -lgdi32 -lshell32
MACSRC=$(CORESRC) src/plat_macos.c
MAC_CFLAGS=-O2 -std=c11 -Isrc -Ivendor -I/usr/local/include -Wall -Wno-unused-function

SHADERS=terrain.vert terrain.tesc terrain.tese terrain.frag terrain_static.vert sky.vert sky.frag grass.vert grass.frag rock.vert rock.frag water.vert water.frag tree.vert tree.frag fullscreen.vert post_final.frag lut_transmittance.frag lut_multiscatter.frag lut_skyview.frag
SPV=$(patsubst %,$(BUILD)/shaders/%.spv,$(SHADERS)) $(patsubst %,$(BUILD)/shaders/%.mv.spv,$(SHADERS))
SHADER_INC=$(wildcard shaders/inc/*.glsl)

ASSET_IDS=Grass004 Rock035 Ground048

.PHONY : all windows windows_vr macos android android_vr shaders assets run run_vr run_macos push push_vr clean

all : windows

shaders : $(SPV)

$(BUILD)/shaders/%.mv.spv : shaders/% $(SHADER_INC)
	@mkdir -p $(BUILD)/shaders
	$(GLSLC) --target-env=vulkan1.1 -O -DMULTIVIEW -I shaders -o $@ $<

$(BUILD)/shaders/%.spv : shaders/% $(SHADER_INC)
	@mkdir -p $(BUILD)/shaders
	$(GLSLC) --target-env=vulkan1.1 -O -I shaders -o $@ $<

assets :
	@mkdir -p assets/tex
	@for id in $(ASSET_IDS); do \
		if [ ! -d assets/tex/$$id ]; then \
			echo "fetching $$id"; \
			mkdir -p assets/tex/$$id; \
			curl -sL "https://ambientcg.com/get?file=$${id}_2K-JPG.zip" -o assets/tex/$$id.zip; \
			unzip -oq assets/tex/$$id.zip -d assets/tex/$$id; \
			rm assets/tex/$$id.zip; \
		fi; \
	done

windows : shaders $(BUILD)/vista.exe

$(BUILD)/vista.exe : $(WINSRC) src/vista.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(WINSRC) $(WINLIBS)

macos : shaders $(BUILD)/vista

$(BUILD)/vista : $(MACSRC) src/vista.h
	@mkdir -p $(BUILD)
	$(CC) $(MAC_CFLAGS) -o $@ $(MACSRC)

run_macos : macos assets
	cd $(BUILD) && VISTA_SMOKE=1 ./vista

windows_vr : shaders $(BUILD)/vista_vr.exe

$(BUILD)/vista_vr.exe : $(WINSRC) src/xr.c src/vista.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DVISTA_VR -o $@ $(WINSRC) src/xr.c $(WINLIBS)
	cp vendor/openxr_loader.dll $(BUILD)/

android : shaders assets
	$(MAKE) -C android

android_vr : shaders assets
	$(MAKE) -C android_vr

run : windows assets
	cd $(BUILD) && ./vista.exe

run_vr : windows_vr assets
	cd $(BUILD) && ./vista_vr.exe

push : android
	$(MAKE) -C android run

push_vr : android_vr
	$(MAKE) -C android_vr run

clean :
	rm -rf $(BUILD)
	$(MAKE) -C android clean
	$(MAKE) -C android_vr clean
