APPNAME=vista
PACKAGENAME=org.gabbo.vista
APKFILE=$(APPNAME).apk
ANDROIDVERSION=29
ANDROIDTARGET=34
ADB?=adb

SRC=../src/vkcore.c ../src/render.c ../src/atmos.c ../src/scene.c ../src/terrain.c ../src/procgen.c ../src/sky.c ../src/veg.c ../src/water.c ../src/game.c ../src/math.c ../src/assets_io.c ../src/plat_android.c ../vendor/volk.c ../vendor/android_native_app_glue.c

CFLAGS?=-ffunction-sections -fdata-sections -Os -std=c11 -Wall -fvisibility=hidden
CFLAGS+=-DANDROID -DANDROIDVERSION=$(ANDROIDVERSION) -DVK_USE_PLATFORM_ANDROID_KHR -DAPPNAME=\"$(APPNAME)\" -I../src -I../vendor -fPIC
LDFLAGS=-Wl,--gc-sections -s -lm -landroid -llog -shared -uANativeActivity_onCreate

UNAME:=$(shell uname -s 2>/dev/null)
OS_NAME=windows-x86_64
ifeq ($(UNAME), Linux)
OS_NAME=linux-x86_64
endif
ifeq ($(UNAME), Darwin)
OS_NAME=darwin-x86_64
endif

SDK_LOCATIONS += $(ANDROID_HOME) $(ANDROID_SDK_ROOT) ~/Android/Sdk
ANDROIDSDK?=$(firstword $(foreach dir, $(SDK_LOCATIONS), $(basename $(dir) ) ) )
NDK?=$(firstword $(ANDROID_NDK) $(ANDROID_NDK_HOME) $(wildcard $(ANDROIDSDK)/ndk/*) )
BUILD_TOOLS?=$(lastword $(wildcard $(ANDROIDSDK)/build-tools/*) )
ANDROID_JAR=$(ANDROIDSDK)/platforms/android-34/android.jar

CC_ARM64=$(NDK)/toolchains/llvm/prebuilt/$(OS_NAME)/bin/aarch64-linux-android$(ANDROIDVERSION)-clang
AAPT=$(BUILD_TOOLS)/aapt

STOREPASS?=password
KEYSTOREFILE=../vista.keystore
ALIASNAME?=standkey

SPV=$(wildcard ../build/shaders/*.spv)

all : $(APKFILE)
	@echo "android ok: $(APKFILE)"

keystore : $(KEYSTOREFILE)

$(KEYSTOREFILE) :
	keytool -genkey -v -keystore $(KEYSTOREFILE) -alias $(ALIASNAME) -keyalg RSA -keysize 2048 -validity 10000 -storepass $(STOREPASS) -keypass $(STOREPASS) -dname "CN=vista, OU=ID, O=vista, L=x, S=x, C=GB"

makecapk/lib/arm64-v8a/lib$(APPNAME).so : $(SRC) ../src/vista.h
	mkdir -p makecapk/lib/arm64-v8a
	$(CC_ARM64) $(CFLAGS) -m64 -o $@ $(SRC) -L$(NDK)/toolchains/llvm/prebuilt/$(OS_NAME)/sysroot/usr/lib/aarch64-linux-android/$(ANDROIDVERSION) $(LDFLAGS)

$(APKFILE) : makecapk/lib/arm64-v8a/lib$(APPNAME).so AndroidManifest.xml $(KEYSTOREFILE) $(SPV)
	mkdir -p makecapk/assets/shaders
	cp -r ../build/shaders/* makecapk/assets/shaders/
	cp -rL ../assets/tex makecapk/assets/
	rm -rf temp.apk
	$(AAPT) package -f -F temp.apk -I $(ANDROID_JAR) -M AndroidManifest.xml -S Sources/res -A makecapk/assets --target-sdk-version $(ANDROIDTARGET)
	unzip -oq temp.apk -d makecapk
	rm -rf makecapk.apk
	cd makecapk && zip -D4rq ../makecapk.apk . && zip -D0rq ../makecapk.apk ./resources.arsc ./AndroidManifest.xml
	rm -rf $(APKFILE)
	$(BUILD_TOOLS)/zipalign 4 makecapk.apk $(APKFILE)
	$(BUILD_TOOLS)/apksigner sign --key-pass pass:$(STOREPASS) --ks-pass pass:$(STOREPASS) --ks $(KEYSTOREFILE) $(APKFILE)
	rm -rf temp.apk makecapk.apk
	@ls -l $(APKFILE)

push : $(APKFILE)
	$(ADB) install -r $(APKFILE)

run : push
	$(ADB) shell am start -n $(PACKAGENAME)/android.app.NativeActivity

clean :
	rm -rf temp.apk makecapk.apk makecapk $(APKFILE)
