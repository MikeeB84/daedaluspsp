# DaedalusX64 CMake Build File (PSP only)
# Created and Maintained by Wally

if(NOT PSP)
	message(FATAL_ERROR "DaedalusX64 only targets the PSP. Configure with psp-cmake or run ./build_daedalus.sh")
endif()

set(CMAKE_CXX_STANDARD 23)

list(APPEND plat_main )

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	message(STATUS "GCC detected: ${CMAKE_CXX_COMPILER_VERSION}")

	if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
		message(STATUS "GCC version is older than 13. Using LibFormat.")
		option(GCC_LEGACY "Use LibFormat" ON)
	endif()
endif()

## if DEBUG is added to the build_daedalus script it will execute the Debug section otherwise by default do release.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	message("Debug Build")

	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
	add_compile_definitions(DAEDALUS_CONFIG_VERSION="Dev") ## Show the build type in the UI
	option(DAEDALUS_ENABLE_ASSERTS "Enable Static Asserts" ON)
	option(DAEDALUS_DEBUG_CONSOLE "Enable Debug Console" ON)
	option(DAEDALUS_DEBUG_DISPLAYLIST "Options for debugging the display list, useful for determining texture issues" OFF)
	option(DAEDALUS_LOG "Enable various logging" ON)
	option(DAEDALUS_SILENT "Shhh, no debug console message, on by default" OFF)
	add_compile_options("-Wall")
	add_compile_options("-Wextra")
else()
	message("Release Build")
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
	add_compile_definitions(DAEDALUS_CONFIG_VERSION="Release") ## Show the build type in the UI
	option(DAEDALUS_SILENT "Shhh, no debug console message, on by default" ON)
	option(DAEDALUS_DEBUG_CONSOLE "Enable Debug Console" OFF)
	message (${CMAKE_CXX_FLAGS_RELEASE})
endif()

# General options
option(DAEDALUS_BATCH_TEST_ENABLED "Enable batch testing for compatibility" OFF)
option(DAEDALUS_COMPRESSED_ROM_SUPPORT "Enable Compressed ROM Support" OFF)
option(DAEDALUS_LOG "Log stuff " OFF) ## Needs to be off unless debugging

# Additional Debug Options
option(SHOW_MEM "Show memory usage" OFF)
option(DAEDALUS_DEBUG_MEMORY "Debug memory issues" OFF)
option(DAEDALUS_DEBUG_PIF "Enable Debugging Options for Peripheral Interface (PIF)" OFF)
option(DAEDALUS_DEBUG_DYNAREC "Enable Various Debugging options for the Dynarec" OFF)
option(LOG_ABORTED_TRACES "Log aborted DynaRec Traces to file" OFF)
option(DAEDALUS_ENABLE_SYNCHRONISATION "Enable for sync testing" OFF)
option(DAEDALUS_ENABLE_PROFILING "Enable built in profiler" OFF)
option(DAEDALUS_PROFILE_EXECUTION "Keep track of various execution stats" OFF)
option(DUMPOSFUNCTIONS "Dump OSHLE Functions" OFF)
option(DAEDALUS_FRAMERATE_ANALYSIS "Output a CSV file containing framerate analysis" OFF)
option(DAEDALUS_DEBUG_AUDIO "Output some Audio Samples to raw file" OFF)

# PSP options
option(DAEDALUS_DIALOGS "Enable UI Dialogs" OFF)
option(DAEDALUS_ENABLE_DYNAREC "Enable Dynamic Recompilation" ON)
option(DAEDALUS_PSP "PSP specific Options" ON)
option(DAEDALUS_PSP_USE_ME "Use the Media Engine" ON)
option(DAEDALUS_PSP_USE_VFPU "Enable VFPU Instructions" ON)
option(DAEDALUS_SIM_DOUBLES "Simulate Doubles" ON)
option(DAEDALUS_ENABLE_OS_HOOKS "Enable High Level Emulation Hooks" ON)
option(DAEDALUS_ENDIAN_LITTLE "Little Endian Processor" ON)

# Profiler Options
option(DAEDALUS_PROFILE_AUDIOPLUGIN "" OFF)
option(DAEDALUS_PROFILE_CORE "" OFF)
option(DAEDALUS_PROFILE_DEBUG "" OFF)
option(DAEDALUS_PROFILE_DYNAREC "" OFF)
option(DAEDALUS_PROFILE_GRAPHICS "" OFF)
option(DAEDALUS_PROFILE_HLEAUDIO "" OFF)
option(DAEDALUS_PROFILE_HLEGRAPHICS "" OFF)
option(DAEDALUS_PROFILE_INPUT "" OFF)
option(DAEDALUS_PROFILE_INTERFACE "" OFF)
option(DAEDALUS_PROFILE_OSHLE "" OFF)
option(DAEDALUS_PROFILE_ROMFILE "" OFF)
option(DAEDALUS_PROFILE_SYSTEM "" OFF)
option(DAEDALUS_PROFILE_UI "" OFF)
option(DAEDALUS_PROFILE_UTILITY "" OFF)

option(DAEDALUS_PROFILE_UTILITY_PSP "" OFF)
option(DAEDALUS_PROFILE_HLEGRAPHICS_PSP "" OFF)
option(DAEDALUS_PROFILE_GRAPHICS_PSP "" OFF)
option(DAEDALUS_PROFILE_DEBUG_PSP "" OFF)

if(DAEDALUS_GPROF)
	link_libraries(-pg)
endif()

# Needed below the options to post process
include(${PROJECT_SOURCE_DIR}/Options.cmake)

#Setup directories to include
list(APPEND include_dirs "${PROJECT_SOURCE_DIR}/Source")
include_directories( BEFORE ${include_dirs})

if(GCC_LEGACY)
	find_package(fmt REQUIRED)
endif(GCC_LEGACY)

find_package(PNG REQUIRED)
link_libraries(PNG::PNG)

find_package(ZLIB REQUIRED)
link_libraries(ZLIB::ZLIB)

add_definitions(-D_USE_MATH_DEFINES)

## Daedalus Core Build
add_subdirectory(Core)
add_subdirectory(Debug)
add_subdirectory(DynaRec)
add_subdirectory(Graphics)
add_subdirectory(HLEAudio)
add_subdirectory(HLEGraphics)
add_subdirectory(Interface)
add_subdirectory(Input)
add_subdirectory(OSHLE)
add_subdirectory(RomFile)
add_subdirectory(System)
add_subdirectory(Utility)
add_subdirectory(UI)
add_subdirectory(SysPSP)

# ---  Build ----
add_executable(daedalus ${plat_main})

if(GCC_LEGACY)
	target_link_libraries(daedalus PRIVATE ${daed_libs} ${default_libraries} ${sys_libraries} fmt::fmt)
else()
	target_link_libraries(daedalus PRIVATE ${daed_libs} ${default_libraries} ${sys_libraries} )
endif(GCC_LEGACY)

# --- Post Build ---
## PRX Builds add about 600kb to the binary as they're not stripped at this point.
create_pbp_file(
	TARGET daedalus
	PREVIEW_PATH NULL
	ICON_PATH ${PROJECT_SOURCE_DIR}/Source/SysPSP/Resources/eboot_icons/icon0.png
	BACKGROUND_PATH ${PROJECT_SOURCE_DIR}/Source/SysPSP/Resources/eboot_icons/pic1.png
	TITLE DaedalusX64
	MEMSIZE 1	# Request the extra memory of PSP 2000 and later (newer toolchains default to 2 = 24MB only)
	BUILD_PRX
	)

## Install and setup basic directories
install(DIRECTORY ${PROJECT_BINARY_DIR}/../Data/ DESTINATION DaedalusX64)
install(DIRECTORY DESTINATION DaedalusX64/Roms)
install(DIRECTORY DESTINATION DaedalusX64/SaveGames)
install(DIRECTORY DESTINATION DaedalusX64/SaveStates)
