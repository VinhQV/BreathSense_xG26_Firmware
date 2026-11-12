# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(
	BreathSense_Relay
	VERSION 1.0
	LANGUAGES C CXX ASM
)

# Include the definition of the slc_BreathSense_Relay target,
# which contains the content of the SLC project
include(BreathSense_Relay.cmake)

add_executable(BreathSense_Relay
    # Add additional sources here
)

target_include_directories(BreathSense_Relay PUBLIC
    # Add additional include paths here
)

target_compile_definitions(BreathSense_Relay PUBLIC
    # Add additional macros here
)

target_compile_options(BreathSense_Relay PUBLIC
    # Set additional compiler flags here
)

target_link_options(BreathSense_Relay PUBLIC
    # Set additional linker flags here
)

# Link with the content defined in the SLC project
target_link_libraries(BreathSense_Relay PRIVATE
    slc
)

# Include managed project content if available
include(BreathSense_Relay_project.cmake OPTIONAL RESULT_VARIABLE managed_project)
if(managed_project)
    message(STATUS "Using managed project content from ${managed_project}")
endif()

# Force the gcc linker command
set_target_properties(BreathSense_Relay PROPERTIES LINKER_LANGUAGE C)

# Create .bin, .hex and .s37 artifacts after building the project
add_custom_command(TARGET BreathSense_Relay
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_SREC_CMD} "$<TARGET_FILE:BreathSense_Relay>" "$<TARGET_FILE_DIR:BreathSense_Relay>/$<TARGET_FILE_BASE_NAME:BreathSense_Relay>.s37"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_IHEX_CMD} "$<TARGET_FILE:BreathSense_Relay>" "$<TARGET_FILE_DIR:BreathSense_Relay>/$<TARGET_FILE_BASE_NAME:BreathSense_Relay>.hex"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_BIN_CMD}  "$<TARGET_FILE:BreathSense_Relay>" "$<TARGET_FILE_DIR:BreathSense_Relay>/$<TARGET_FILE_BASE_NAME:BreathSense_Relay>.bin" 
)

# Run post-build pipeline to perform additional post-processing
if(post_build_command)
add_custom_command(TARGET BreathSense_Relay
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)
endif()