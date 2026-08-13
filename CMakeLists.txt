cmake_minimum_required(VERSION 3.20)

# macOS toolchain settings have to be in the cache before project() runs, or the
# compiler probe picks the wrong architecture. They are inert on other platforms.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures for the universal binary")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum supported macOS version")

project(InnerGlowFX
    VERSION 1.0.0
    DESCRIPTION "The Inner Glow layer style as a real After Effects effect"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# After Effects SDK location
#
# The SDK is not redistributable, so it is never vendored into this repository.
# It is located through the AE_SDK_PATH cache variable, falling back to the
# environment variable of the same name.
# ---------------------------------------------------------------------------

if(NOT AE_SDK_PATH AND DEFINED ENV{AE_SDK_PATH})
    set(AE_SDK_PATH "$ENV{AE_SDK_PATH}")
endif()

set(AE_SDK_PATH "${AE_SDK_PATH}" CACHE PATH
    "Root of the After Effects SDK (the folder that contains Examples/)")

if(NOT AE_SDK_PATH)
    message(FATAL_ERROR
        "AE_SDK_PATH is not set.\n"
        "Download the After Effects SDK from https://developer.adobe.com/after-effects/ "
        "(free Adobe ID required), unpack it, then either set the AE_SDK_PATH environment "
        "variable or configure with -DAE_SDK_PATH=<path>.\n"
        "The path must be the folder that directly contains Examples/.")
endif()

file(TO_CMAKE_PATH "${AE_SDK_PATH}" AE_SDK_PATH)

set(AE_SDK_HEADERS   "${AE_SDK_PATH}/Examples/Headers")
set(AE_SDK_UTIL      "${AE_SDK_PATH}/Examples/Util")
set(AE_SDK_RESOURCES "${AE_SDK_PATH}/Examples/Resources")

if(NOT EXISTS "${AE_SDK_HEADERS}/AE_Effect.h")
    message(FATAL_ERROR
        "AE_Effect.h was not found under AE_SDK_PATH.\n"
        "  AE_SDK_PATH = ${AE_SDK_PATH}\n"
        "  expected    = ${AE_SDK_HEADERS}/AE_Effect.h\n"
        "Point AE_SDK_PATH at the SDK root, i.e. the folder that directly contains Examples/.")
endif()

message(STATUS "After Effects SDK: ${AE_SDK_PATH}")

# ---------------------------------------------------------------------------
# The plug-in target
# ---------------------------------------------------------------------------

add_library(InnerGlowFX MODULE
    src/InnerGlowFX.cpp
    src/InnerGlowFX.h
    src/GlowRender.cpp
    src/GlowRender.h)

target_include_directories(InnerGlowFX PRIVATE
    "${AE_SDK_HEADERS}"
    "${AE_SDK_HEADERS}/SP"
    "${AE_SDK_UTIL}"
    "${AE_SDK_RESOURCES}")

set_target_properties(InnerGlowFX PROPERTIES
    PREFIX ""
    OUTPUT_NAME "InnerGlowFX")

# ---------------------------------------------------------------------------
# Windows: x64 DLL renamed to .aex, with the PiPL compiled in as a resource
# ---------------------------------------------------------------------------

if(WIN32)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Windows builds are x64 only. Configure with -A x64.")
    endif()

    target_compile_definitions(InnerGlowFX PRIVATE MSWindows WIN32 _WINDOWS)

    # /MT so the plug-in does not need a VC++ redistributable on the user's machine.
    set_property(TARGET InnerGlowFX PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    set_target_properties(InnerGlowFX PROPERTIES SUFFIX ".aex")

    set(PIPL_TOOL "${AE_SDK_RESOURCES}/PiPLtool.exe")
    if(NOT EXISTS "${PIPL_TOOL}")
        message(FATAL_ERROR
            "PiPLtool.exe was not found at ${PIPL_TOOL}.\n"
            "Without it the PiPL resource cannot be built and After Effects will not "
            "show the effect at all.")
    endif()

    set(PIPL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/resources/InnerGlowFX_PiPL.r")
    set(PIPL_DIR "${CMAKE_CURRENT_BINARY_DIR}/pipl")
    set(PIPL_RR  "${PIPL_DIR}/InnerGlowFX_PiPL.rr")
    set(PIPL_RRC "${PIPL_DIR}/InnerGlowFX_PiPL.rrc")
    set(PIPL_RC  "${PIPL_DIR}/InnerGlowFX_PiPL.rc")

    # Three stages, exactly as the SDK sample projects do it:
    #   1. run the C preprocessor over the .r         -> .rr
    #   2. PiPLtool turns that into resource data     -> .rrc
    #   3. preprocess once more into a plain .rc      -> .rc
    # /EP /P writes preprocessed output straight to the file named by /Fi, which
    # avoids shell redirection and keeps this working under any generator.
    add_custom_command(
        OUTPUT "${PIPL_RC}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${PIPL_DIR}"
        COMMAND "${CMAKE_CXX_COMPILER}" /nologo /EP /P "/Fi${PIPL_RR}"
                /I "${AE_SDK_HEADERS}" "${PIPL_SRC}"
        COMMAND "${PIPL_TOOL}" "${PIPL_RR}" "${PIPL_RRC}"
        COMMAND "${CMAKE_CXX_COMPILER}" /nologo /EP /P /D MSWindows
                "/Fi${PIPL_RC}" "${PIPL_RRC}"
        DEPENDS "${PIPL_SRC}"
        COMMENT "Compiling the PiPL resource"
        VERBATIM)

    target_sources(InnerGlowFX PRIVATE "${PIPL_RC}")
    set_source_files_properties("${PIPL_RC}" PROPERTIES GENERATED TRUE)

    set(AE_PLUGIN_INSTALL_DIR
        "C:/Program Files/Adobe/Adobe After Effects 2025/Support Files/Plug-ins/Effects"
        CACHE PATH "Folder the built plug-in is copied into by the install target")
endif()

# ---------------------------------------------------------------------------
# macOS: universal .plugin bundle with the PiPL Rez'd into Contents/Resources
# ---------------------------------------------------------------------------

if(APPLE)
    target_compile_definitions(InnerGlowFX PRIVATE MacOS)

    set(MACOSX_BUNDLE_EXECUTABLE_NAME "InnerGlowFX")
    set(MACOSX_BUNDLE_BUNDLE_NAME "InnerGlowFX")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.draexon.InnerGlowFX")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 DRAEXON")

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        @ONLY)

    set_target_properties(InnerGlowFX PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "plugin"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")

    find_program(REZ_EXECUTABLE Rez
        HINTS /usr/bin /Applications/Xcode.app/Contents/Developer/usr/bin)
    if(NOT REZ_EXECUTABLE)
        message(FATAL_ERROR
            "Rez was not found. Install the Xcode command line tools "
            "(xcode-select --install). Without Rez the PiPL cannot be built and "
            "After Effects will not show the effect at all.")
    endif()

    add_custom_command(TARGET InnerGlowFX POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:InnerGlowFX>/Resources"
        COMMAND "${REZ_EXECUTABLE}"
                -o "$<TARGET_BUNDLE_CONTENT_DIR:InnerGlowFX>/Resources/InnerGlowFX.rsrc"
                -d __MACH__ -useDF
                -i "${AE_SDK_HEADERS}"
                -i "${AE_SDK_RESOURCES}"
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/InnerGlowFX_PiPL.r"
        COMMENT "Compiling the PiPL resource"
        VERBATIM)

    set(AE_PLUGIN_INSTALL_DIR
        "/Applications/Adobe After Effects 2025/Plug-ins/Effects"
        CACHE PATH "Folder the built plug-in is copied into by the install target")
endif()

# ---------------------------------------------------------------------------
# install
# ---------------------------------------------------------------------------

install(TARGETS InnerGlowFX
    LIBRARY DESTINATION "${AE_PLUGIN_INSTALL_DIR}"
    BUNDLE  DESTINATION "${AE_PLUGIN_INSTALL_DIR}")

message(STATUS "Plug-in install folder: ${AE_PLUGIN_INSTALL_DIR}")
