# Copyright (C) 2015 TeamWin Recovery Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

ifneq ($(TW_EXCLUDE_DEFAULT_USB_INIT), true)

include $(CLEAR_VARS)
LOCAL_MODULE := init.recovery.usb.rc
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES

# Cannot send to TARGET_RECOVERY_ROOT_OUT since build system wipes init*.rc
# during ramdisk creation and only allows init.recovery.*.rc files to be copied
# from TARGET_ROOT_OUT thereafter
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)

endif

include $(CLEAR_VARS)
LOCAL_MODULE := init.recovery.service.rc
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

LOCAL_SRC_FILES := init.recovery.service22.rc
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := init.recovery.hlthchrg.rc
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

LOCAL_SRC_FILES := init.recovery.hlthchrg26.rc
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := init.recovery.ldconfig.rc
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

LOCAL_SRC_FILES := init.recovery.ldconfig.rc
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := nano.rc
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/etc/init

LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
include $(BUILD_PREBUILT)

ifneq ($(filter $(AB_OTA_UPDATER) $(PRODUCT_USE_DYNAMIC_PARTITIONS) $(TW_INCLUDE_CRYPTO), true),)
	include $(CLEAR_VARS)
	LOCAL_MODULE := hwservicemanager.rc
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/etc/init

	LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
	include $(BUILD_PREBUILT)

	include $(CLEAR_VARS)
	LOCAL_MODULE := vndservicemanager.rc
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/etc/init

	LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
	include $(BUILD_PREBUILT)

	include $(CLEAR_VARS)
	LOCAL_MODULE := keystore2.rc
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/etc/init

	LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
	include $(BUILD_PREBUILT)

	include $(CLEAR_VARS)
	LOCAL_MODULE := android.system.keystore2-service.xml
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/etc/vintf/manifest
	LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
	include $(BUILD_PREBUILT)
endif

ifeq ($(AB_OTA_UPDATER),true)
	include $(CLEAR_VARS)
	LOCAL_MODULE := android.hardware.boot@1.0-service.rc
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/etc/init
	LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
	include $(BUILD_PREBUILT)

	include $(CLEAR_VARS)
	LOCAL_MODULE := android.hardware.boot@1.1-service.rc
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/etc/init
	LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
	include $(BUILD_PREBUILT)

	include $(CLEAR_VARS)
	LOCAL_MODULE := android.hardware.boot@1.1.xml
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/vendor/etc/vintf/manifest
	LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
	include $(BUILD_PREBUILT)

	include $(CLEAR_VARS)
	LOCAL_MODULE := android.hardware.boot@1.2-service.rc
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/etc/init
	LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
	include $(BUILD_PREBUILT)

	include $(CLEAR_VARS)
	LOCAL_MODULE := android.hardware.boot@1.2.xml
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/vendor/etc/vintf/manifest
	LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
	include $(BUILD_PREBUILT)
endif

include $(CLEAR_VARS)
LOCAL_MODULE := android.hardware.health@2.1-service.rc
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/etc/init
LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := android.hardware.health@2.1.xml
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/vendor/etc/vintf/manifest
LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := android.hardware.health@2.0-service.rc
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/etc/init
LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
include $(BUILD_PREBUILT)

ifeq ($(PRODUCT_USE_DYNAMIC_PARTITIONS),true)
	include $(CLEAR_VARS)
	LOCAL_MODULE := lpdumpd.rc
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/etc/init
	LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
	include $(BUILD_PREBUILT)
endif

ifneq ($(TW_INCLUDE_CRYPTO),)
	ifneq ($(TW_INCLUDE_CRYPTO_FBE),)
		include $(CLEAR_VARS)
		LOCAL_MODULE := servicemanager.rc
		LOCAL_MODULE_TAGS := optional
		LOCAL_MODULE_CLASS := EXECUTABLES
		LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/etc/init

		LOCAL_SRC_FILES := init/$(LOCAL_MODULE)
		include $(BUILD_PREBUILT)
	endif
endif

ifeq ($(TWRP_INCLUDE_LOGCAT), true)
    ifeq ($(TARGET_USES_LOGD), true)

        include $(CLEAR_VARS)
        LOCAL_MODULE := init.recovery.logd.rc
        LOCAL_MODULE_TAGS := optional
        LOCAL_MODULE_CLASS := EXECUTABLES

        # Cannot send to TARGET_RECOVERY_ROOT_OUT since build system wipes init*.rc
        # during ramdisk creation and only allows init.recovery.*.rc files to be copied
        # from TARGET_ROOT_OUT thereafter
        LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

        LOCAL_SRC_FILES := $(LOCAL_MODULE)
        include $(BUILD_PREBUILT)
    endif
endif

ifeq ($(TW_USE_TOOLBOX), true)
    include $(CLEAR_VARS)
    LOCAL_MODULE := init.recovery.mksh.rc
    LOCAL_MODULE_TAGS := optional
    LOCAL_MODULE_CLASS := EXECUTABLES

    # Cannot send to TARGET_RECOVERY_ROOT_OUT since build system wipes init*.rc
    # during ramdisk creation and only allows init.recovery.*.rc files to be copied
    # from TARGET_ROOT_OUT thereafter
    LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

    LOCAL_SRC_FILES := $(LOCAL_MODULE)
    include $(BUILD_PREBUILT)
endif
