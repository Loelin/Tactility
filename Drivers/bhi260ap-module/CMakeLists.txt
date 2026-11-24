cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/../../Buildscripts/module.cmake")

file(GLOB_RECURSE SOURCE_FILES "source/*.c*" "private/bhi260x/*.c")

tactility_add_module(bhi260ap-module
    SRCS ${SOURCE_FILES}
    INCLUDE_DIRS include/
    PRIV_INCLUDE_DIRS private/ private/bhi260x
    REQUIRES TactilityKernel
)

# The Bosch KLIO firmware is proprietary and intentionally not committed.
# Fetch it locally with fetch_firmware.py (see private/firmware/README.md).
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/private/firmware/bosch_bhi260_klio.h")
    if (DEFINED ENV{ESP_IDF_VERSION})
        target_compile_definitions(${COMPONENT_LIB} PRIVATE BHI260AP_HAVE_FIRMWARE=1)
    else()
        target_compile_definitions(bhi260ap-module PRIVATE BHI260AP_HAVE_FIRMWARE=1)
    endif()
endif()
