cmake_minimum_required(VERSION "3.25")

project(
    BreathSense_xG26_Node_V2
    VERSION 1.0
    LANGUAGES C CXX ASM
)

include(BreathSense_xG26_Node_V2.cmake)

if(NOT TARGET slc)
    message(FATAL_ERROR
        "Silicon Labs target 'slc' was not created. "
        "Check cmake_gcc/BreathSense_xG26_Node_V2.cmake."
    )
endif()

add_executable(BreathSense_xG26_Node_V2)

target_link_libraries(
    BreathSense_xG26_Node_V2
    PRIVATE
    slc
)

set_target_properties(
    BreathSense_xG26_Node_V2
    PROPERTIES
    LINKER_LANGUAGE C
)

include(
    BreathSense_xG26_Node_V2_project.cmake
    OPTIONAL
    RESULT_VARIABLE managed_project
)

if(managed_project)
    message(STATUS
        "Using managed project content from ${managed_project}"
    )
endif()

add_custom_command(
    TARGET BreathSense_xG26_Node_V2
    POST_BUILD

    COMMAND
        ${CMAKE_OBJCOPY}
        ${OBJCOPY_SREC_CMD}
        "$<TARGET_FILE:BreathSense_xG26_Node_V2>"
        "$<TARGET_FILE_DIR:BreathSense_xG26_Node_V2>/$<TARGET_FILE_BASE_NAME:BreathSense_xG26_Node_V2>.s37"

    COMMAND
        ${CMAKE_OBJCOPY}
        ${OBJCOPY_IHEX_CMD}
        "$<TARGET_FILE:BreathSense_xG26_Node_V2>"
        "$<TARGET_FILE_DIR:BreathSense_xG26_Node_V2>/$<TARGET_FILE_BASE_NAME:BreathSense_xG26_Node_V2>.hex"

    COMMAND
        ${CMAKE_OBJCOPY}
        ${OBJCOPY_BIN_CMD}
        "$<TARGET_FILE:BreathSense_xG26_Node_V2>"
        "$<TARGET_FILE_DIR:BreathSense_xG26_Node_V2>/$<TARGET_FILE_BASE_NAME:BreathSense_xG26_Node_V2>.bin"
)

if(post_build_command)
    add_custom_command(
        TARGET BreathSense_xG26_Node_V2
        POST_BUILD
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
        COMMAND ${post_build_command}
    )
endif()

get_filename_component(
    BS_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/.."
    ABSOLUTE
)

set(BS_CORE
    "${BS_ROOT}/src/core"
)

set(BS_INDICATOR
    "${BS_ROOT}/src/indicator"
)

message(STATUS "BreathSense project root: ${BS_ROOT}")
message(STATUS "BreathSense core module: ${BS_CORE}")
message(STATUS "BreathSense indicator module: ${BS_INDICATOR}")

target_include_directories(
    slc
    PRIVATE
    "${BS_ROOT}"
    "${BS_CORE}"
    "${BS_INDICATOR}"
)

get_target_property(
    BS_SLC_SOURCES
    slc
    SOURCES
)

if(BS_SLC_SOURCES
   AND NOT BS_SLC_SOURCES STREQUAL "BS_SLC_SOURCES-NOTFOUND")

    set(BS_FILTERED_SLC_SOURCES "")

    foreach(BS_SOURCE IN LISTS BS_SLC_SOURCES)
        get_filename_component(
            BS_SOURCE_NAME
            "${BS_SOURCE}"
            NAME
        )

        set(BS_REMOVE_SOURCE FALSE)

        if(BS_SOURCE_NAME STREQUAL "sl_tflite_micro_init.cc"
           OR BS_SOURCE_NAME STREQUAL "sl_tflite_micro_model.c"
           OR BS_SOURCE_NAME STREQUAL "voice_control_light.cc"
           OR BS_SOURCE_NAME STREQUAL "recognize_commands.cc"
           OR BS_SOURCE_NAME STREQUAL "breathsense_ai_mock.c"
           OR BS_SOURCE_NAME STREQUAL "breathsense_pawr.c"
           OR BS_SOURCE_NAME STREQUAL "breathsense_offline_transport.c"
           OR BS_SOURCE_NAME STREQUAL "breathsense_event_protocol.c")
            set(BS_REMOVE_SOURCE TRUE)

        elseif(BS_SOURCE_NAME MATCHES
               "^breathsense_(golden.*|mfcc_golden.*|dsp_probe.*|mic_probe.*|stage2_golden.*|stage2_dsp_golden.*)\\.(c|cc)$")
            set(BS_REMOVE_SOURCE TRUE)
        endif()

        if(NOT BS_REMOVE_SOURCE)
            list(APPEND
                BS_FILTERED_SLC_SOURCES
                "${BS_SOURCE}"
            )
        endif()
    endforeach()

    set_property(
        TARGET slc
        PROPERTY SOURCES
        "${BS_FILTERED_SLC_SOURCES}"
    )
endif()

function(bs_add_source_if_missing TARGET_NAME SOURCE_PATH)
    if(NOT EXISTS "${SOURCE_PATH}")
        message(FATAL_ERROR
            "BreathSense required source file not found: ${SOURCE_PATH}"
        )
    endif()

    get_filename_component(
        BS_WANTED_NAME
        "${SOURCE_PATH}"
        NAME
    )

    get_target_property(
        BS_CURRENT_SOURCES
        ${TARGET_NAME}
        SOURCES
    )

    set(BS_SOURCE_FOUND FALSE)

    if(BS_CURRENT_SOURCES
       AND NOT BS_CURRENT_SOURCES STREQUAL "BS_CURRENT_SOURCES-NOTFOUND")

        foreach(BS_CURRENT_SOURCE IN LISTS BS_CURRENT_SOURCES)
            get_filename_component(
                BS_CURRENT_NAME
                "${BS_CURRENT_SOURCE}"
                NAME
            )

            if(BS_CURRENT_NAME STREQUAL BS_WANTED_NAME)
                set(BS_SOURCE_FOUND TRUE)
                break()
            endif()
        endforeach()
    endif()

    if(NOT BS_SOURCE_FOUND)
        target_sources(
            ${TARGET_NAME}
            PRIVATE
            "${SOURCE_PATH}"
        )

        message(STATUS
            "BreathSense source added: ${BS_WANTED_NAME}"
        )
    else()
        message(STATUS
            "BreathSense source already managed: ${BS_WANTED_NAME}"
        )
    endif()
endfunction()

bs_add_source_if_missing(slc "${BS_ROOT}/breathsense_model_data.cc")
bs_add_source_if_missing(slc "${BS_ROOT}/breathsense_mfcc_coefficients.cc")
bs_add_source_if_missing(slc "${BS_ROOT}/breathsense_mfcc_runtime.cc")
bs_add_source_if_missing(slc "${BS_ROOT}/breathsense_inference_runtime.cc")
bs_add_source_if_missing(slc "${BS_ROOT}/breathsense_ai_live.cc")

bs_add_source_if_missing(slc "${BS_ROOT}/breathsense_stage2_model_data.cc")
bs_add_source_if_missing(slc "${BS_ROOT}/breathsense_stage2_runtime.cc")
bs_add_source_if_missing(slc "${BS_ROOT}/breathsense_stage2_logmel_coefficients.cc")
bs_add_source_if_missing(slc "${BS_ROOT}/breathsense_stage2_logmel_runtime.cc")

bs_add_source_if_missing(slc "${BS_CORE}/breathsense_event.c")
bs_add_source_if_missing(slc "${BS_CORE}/breathsense_telemetry.c")

bs_add_source_if_missing(slc "${BS_ROOT}/breathsense_sensor.c")

bs_add_source_if_missing(slc "${BS_ROOT}/breathsense_ble.c")
bs_add_source_if_missing(slc "${BS_ROOT}/breathsense_ble_advertising.c")
bs_add_source_if_missing(slc "${BS_ROOT}/breathsense_ble_tx.c")

bs_add_source_if_missing(slc "${BS_INDICATOR}/breathsense_indicator.c")
bs_add_source_if_missing(slc "${BS_ROOT}/breathsense_tflite_autoinit_stub.cc")

set(BS_REQUIRED_HEADERS
    "${BS_ROOT}/breathsense_ai.h"
    "${BS_ROOT}/breathsense_ai_product_config.h"
    "${BS_ROOT}/breathsense_model_data.h"
    "${BS_ROOT}/breathsense_mfcc_coefficients.h"
    "${BS_ROOT}/breathsense_mfcc_config.h"
    "${BS_ROOT}/breathsense_mfcc_runtime.h"
    "${BS_ROOT}/breathsense_inference_runtime.h"

    "${BS_ROOT}/breathsense_stage2_model_data.h"
    "${BS_ROOT}/breathsense_stage2_runtime.h"
    "${BS_ROOT}/breathsense_stage2_logmel_coefficients.h"
    "${BS_ROOT}/breathsense_stage2_logmel_runtime.h"

    "${BS_CORE}/breathsense_protocol_config.h"
    "${BS_CORE}/breathsense_event.h"
    "${BS_CORE}/breathsense_telemetry.h"

    "${BS_ROOT}/breathsense_sensor.h"

    "${BS_ROOT}/breathsense_ble.h"
    "${BS_ROOT}/breathsense_ble_advertising.h"
    "${BS_ROOT}/breathsense_ble_tx.h"

    "${BS_INDICATOR}/breathsense_indicator.h"
)

foreach(BS_REQUIRED_HEADER IN LISTS BS_REQUIRED_HEADERS)
    if(NOT EXISTS "${BS_REQUIRED_HEADER}")
        message(FATAL_ERROR
            "BreathSense required header file not found: "
            "${BS_REQUIRED_HEADER}"
        )
    endif()
endforeach()

get_target_property(
    BS_FINAL_SOURCES
    slc
    SOURCES
)

set(BS_STAGE2_RUNTIME_FOUND FALSE)

foreach(BS_FINAL_SOURCE IN LISTS BS_FINAL_SOURCES)
    get_filename_component(
        BS_FINAL_NAME
        "${BS_FINAL_SOURCE}"
        NAME
    )

    if(BS_FINAL_NAME STREQUAL "breathsense_stage2_runtime.cc")
        set(BS_STAGE2_RUNTIME_FOUND TRUE)
        message(STATUS
            "Stage 2 runtime source confirmed: ${BS_FINAL_SOURCE}"
        )
        break()
    endif()
endforeach()

if(NOT BS_STAGE2_RUNTIME_FOUND)
    message(FATAL_ERROR
        "breathsense_stage2_runtime.cc is missing from target slc"
    )
endif()

message(STATUS "BreathSense architecture: event-driven")
message(STATUS "BreathSense app.c: initialization only")
message(STATUS "BreathSense sensor: timer + OSFlagPend")
message(STATUS "BreathSense BLE TX: OSFlagPend, no polling")
message(STATUS "BreathSense Stage 1: enabled")
message(STATUS "BreathSense Stage 2: enabled and source verified")
message(STATUS "BreathSense event protocol: removed")
message(STATUS "BreathSense PAwR application source: excluded")
message(STATUS "BreathSense offline transport: excluded")
message(STATUS "BreathSense autogenerated TFLM model/init: removed")