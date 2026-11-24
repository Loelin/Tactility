file(GLOB_RECURSE SOURCE_FILES source/*.c*)

# Whistle is the local-only dictation engine and is not tracked in the branch. Only require it
# when its source tree is present so a clean checkout still builds.
set(TWATCH_OPTIONAL_REQUIRES "")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../Modules/whistle-module")
    list(APPEND TWATCH_OPTIONAL_REQUIRES whistle-module)
endif()

idf_component_register(
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS "source"
    REQUIRES TactilityKernel Tactility TactilityFreeRtos app-module lvgl-module lvgl-window-manager-module co5300-module
             axp2101-module location-module binfont-module esp_partition esp_driver_gpio esp_http_client esp_netif mbedtls esp_lcd
             esp_lvgl_port ${TWATCH_OPTIONAL_REQUIRES}
)

# Board-local link-time hook (see source/lvgl_stack_internal.cpp): keep the LVGL task stack in
# internal RAM. Core asks esp_lvgl_port for a PSRAM stack, but file access from that task
# (settings saves triggered by UI callbacks) asserts while the flash cache is disabled.
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lvgl_port_init" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,-u,__wrap_lvgl_port_init" APPEND)

# Board-local link-time hook (see source/display_rotation.cpp): force the core's software
# rotation path, since its hardware-rotation flush mismatches the CO5300 full-frame stride.
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=display_has_capability" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,-u,__wrap_display_has_capability" APPEND)

# Board-local link-time hook (see source/lvgl_file_offload.cpp): file checks made from the LVGL
# task's PSRAM stack must run on the file-ops worker, which has an internal stack.
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=file_exists" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,-u,__wrap_file_exists" APPEND)

# Board-local link-time hook (see source/axp2101_gauge.cpp): expose the AXP2101 E-Gauge as
# POWER_SUPPLY_PROP_CAPACITY so the statusbar shows its battery indicator.
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=power_supply_supports_property" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=power_supply_get_property" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,-u,__wrap_power_supply_supports_property" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,-u,__wrap_power_supply_get_property" APPEND)

# Board-local link-time hook (see source/lvgl_flush_align.cpp): apply the CO5300's even-address/
# even-span window rule to dirty areas, and widen PARTIAL-mode areas to full-width strips so
# dirty-rect updates don't shear the image.
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_display_set_flush_cb" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,-u,__wrap_lv_display_set_flush_cb" APPEND)

# Board-local link-time hook (see source/page_change_haptic.cpp): light haptic click when the
# launcher page changes.
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lvgl_page_indicator_set_page" APPEND)

# Board-local link-time hook (see source/touch_haptic.cpp): confirm widget presses with the DRV2605
# haptic motor, without modifying core Tactility source.
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_obj_send_event" APPEND)

# Board-local link-time hook (see source/low_battery_brightness.cpp): cap the backlight at 20%
# while the AXP2101 fuel gauge is below 10%.
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=backlight_set_brightness" APPEND)

# Board-local link-time hook (see source/properties_file_lock.cpp): serialize settings-file I/O so
# concurrent saves (Display app + auto-rotate) don't collide on the shared temporary file.
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=properties_file_open" APPEND)
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=properties_file_close" APPEND)

# Local-only TT9 keyboard feature: install the multi-tap layout on the shared on-screen keyboard
# when source/t9_keyboard.cpp is present.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/source/t9_keyboard.cpp")
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lvgl_software_keyboard_construct" APPEND)
endif()

# Local-only dictation feature: give the Dictation launcher tile the statusbar's microphone glyph
# when source/dictation_icon.cpp is present.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/source/dictation_icon.cpp")
    idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=lv_label_set_text" APPEND)
endif()

