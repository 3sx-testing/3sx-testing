cmake_minimum_required(VERSION 3.24)
project(3sx LANGUAGES C)

# So that GekkoNet builds as a static lib
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ======================================
# Options and platform setup
# ======================================

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_subdirectory(GekkoNet)

# ======================================
# Include directories
# ======================================

include_directories(
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/sdk
    ${PROJECT_SOURCE_DIR}/zlib
    ${PROJECT_SOURCE_DIR}/GekkoNet/GekkoLib/include
)

# ======================================
# Source collection
# ======================================

file(GLOB_RECURSE GAME_SRC src/*.c)
file(GLOB_RECURSE ZLIB_SRC zlib/*.c)

add_executable(3sx MACOSX_BUNDLE
    ${GAME_SRC} ${ZLIB_SRC}
)

# ======================================
# Compiler and linker flags
# ======================================

target_compile_definitions(3sx PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Debug>:NETPLAY_ENABLED>
    $<$<CONFIG:Release>:RELEASE>
    TARGET_SDL3
    _POSIX_C_SOURCE
)

# Feature toggles
target_compile_definitions(3sx PRIVATE
    MEMCARD_DISABLED
)

if(CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    set(DISABLED_WARNINGS
        -Wpointer-to-int-cast
        -Wno-deprecated-non-prototype
        -Wno-tautological-overlap-compare
        -Wno-c2x-extensions
        -Wno-incompatible-pointer-types
        -Wno-pointer-sign
    )
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(DISABLED_WARNINGS
        -Wno-unused-but-set-variable
        -Wno-pointer-sign
        -Wno-parentheses
        -Wno-incompatible-pointer-types
        -Wno-discarded-qualifiers
        -Wno-format-overflow
        -Wno-comment
        -Wno-maybe-uninitialized
        -Wno-array-bounds
        -Wno-aggressive-loop-optimizations
        -Wno-stringop-overflow
        -Wno-restrict
        -Wno-stringop-truncation
    )
endif()

target_compile_options(3sx PRIVATE
    -Wall
    -Werror

    -fno-strict-aliasing

    ${DISABLED_WARNINGS}
)

target_link_libraries(3sx PRIVATE
    m
    GekkoNet
    stdc++
)

set_target_properties(3sx PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist"
)

if(APPLE)
    set_target_properties(3sx PROPERTIES
        OUTPUT_NAME "3SX"
        MACOSX_BUNDLE_BUNDLE_NAME "3SX"
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
elseif(UNIX AND NOT APPLE)
    set_target_properties(3sx PROPERTIES
        INSTALL_RPATH "\$ORIGIN/../lib"
    )
endif()

# ======================================
# Dependencies
# ======================================

set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

# Allow overriding roots (Flatpak uses /app)
if(NOT DEFINED FFMPEG_ROOT)
    set(FFMPEG_ROOT "${THIRD_PARTY_DIR}/ffmpeg/build")
endif()

if(NOT DEFINED SDL3_ROOT)
    set(SDL3_ROOT "${THIRD_PARTY_DIR}/sdl3/build")
endif()

include_directories(
    ${FFMPEG_ROOT}/include
    ${SDL3_ROOT}/include
)

# ---- libcdio handling (Linux) ----
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CDIO REQUIRED libcdio)

    # 1️⃣ Include dirs from pkg-config
    target_include_directories(3sx PRIVATE ${CDIO_INCLUDE_DIRS})

    # 2️⃣ FORCE multiarch include dir (fixes cdio/iso9660.h not found)
    if(EXISTS "/usr/include/aarch64-linux-gnu")
        target_include_directories(3sx PRIVATE /usr/include/aarch64-linux-gnu)
    endif()

    # 3️⃣ Apply raw cflags from pkg-config (sometimes needed)
    if(CDIO_CFLAGS)
        string(REGEX REPLACE ";" " " _cdio_cflags_str "${CDIO_CFLAGS}")
        separate_arguments(_cdio_cflags_list UNIX_COMMAND "${_cdio_cflags_str}")
        target_compile_options(3sx PRIVATE ${_cdio_cflags_list})
    endif()

    # 4️⃣ Link libraries
    target_link_libraries(3sx PRIVATE ${CDIO_LIBRARIES})

    # 5️⃣ Fallback: link iso9660 if separate
    find_library(ISO9660_LIB iso9660)
    if(ISO9660_LIB)
        target_link_libraries(3sx PRIVATE ${ISO9660_LIB})
    endif()

else()
    # Fallback for Apple/Windows where we vendor libcdio as static libs
    set(LIBCDIO_ROOT "${THIRD_PARTY_DIR}/libcdio/build")
    include_directories(${LIBCDIO_ROOT}/include)

    target_link_libraries(3sx PRIVATE
        "${LIBCDIO_ROOT}/lib/libiso9660.a"
        "${LIBCDIO_ROOT}/lib/libcdio.a"
    )
endif()

if(APPLE)
    target_link_libraries(3sx PRIVATE
        ${FFMPEG_ROOT}/lib/libavcodec.dylib
        ${FFMPEG_ROOT}/lib/libavformat.dylib
        ${FFMPEG_ROOT}/lib/libavutil.dylib
        ${FFMPEG_ROOT}/lib/libswresample.dylib
        ${SDL3_ROOT}/lib/libSDL3.0.dylib
        iconv
    )
elseif(WIN32)
    target_link_libraries(3sx PRIVATE
        ${FFMPEG_ROOT}/lib/libavcodec.dll.a
        ${FFMPEG_ROOT}/lib/libavformat.dll.a
        ${FFMPEG_ROOT}/lib/libavutil.dll.a
        ${FFMPEG_ROOT}/lib/libswresample.dll.a
        ${SDL3_ROOT}/lib/libSDL3.dll.a
        iconv
        dbghelp
    )
    target_link_options(3sx PRIVATE
        --for-linker
        --pdb=3sx.pdb
    )
elseif(UNIX)
    target_link_libraries(3sx PRIVATE
        ${FFMPEG_ROOT}/lib/libavcodec.so
        ${FFMPEG_ROOT}/lib/libavformat.so
        ${FFMPEG_ROOT}/lib/libavutil.so
        ${FFMPEG_ROOT}/lib/libswresample.so
        ${SDL3_ROOT}/lib/libSDL3.so
    )
endif()

# ======================================
# Installation
# ======================================

set(PROJECT_LICENSE_FILES
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt"
)

if(APPLE)
    install(TARGETS 3sx
        RUNTIME DESTINATION bin
        BUNDLE DESTINATION .
    )

    file(GLOB FFMPEG_DYLIBS "${FFMPEG_ROOT}/lib/*.dylib")

    install(FILES
        ${FFMPEG_DYLIBS}
        ${SDL3_ROOT}/lib/libSDL3.0.dylib
        DESTINATION 3SX.app/Contents/Frameworks
    )

    install(FILES
        ${PROJECT_LICENSE_FILES}
        DESTINATION 3SX.app/Contents/Resources/licenses
    )
elseif(WIN32)
    install(TARGETS 3sx
		RUNTIME_DEPENDENCY_SET deps
        RUNTIME DESTINATION bin
    )

    execute_process(COMMAND cygpath -m /mingw64/bin
		OUTPUT_VARIABLE binPath
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	
	# automatically copies all the dependent DLLS, and ignores the system ones because they aren't necessary
	install(RUNTIME_DEPENDENCY_SET deps
		DIRECTORIES ${binPath} "${FFMPEG_ROOT}/bin" "${SDL3_ROOT}/bin"
		PRE_EXCLUDE_REGEXES "^api"
		POST_EXCLUDE_REGEXES ".*system32/.*\\.dll"
		DESTINATION bin
	)

    install(FILES
        "build/3sx.pdb"
        DESTINATION bin
    )

    install(FILES
        ${PROJECT_LICENSE_FILES}
        DESTINATION bin/licenses
    )
elseif(UNIX)
    install(TARGETS 3sx
        RUNTIME DESTINATION bin
    )

    file(GLOB FFMPEG_SO "${FFMPEG_ROOT}/lib/*.so*")

    install(FILES
        ${FFMPEG_SO}
        ${SDL3_ROOT}/lib/libSDL3.so
        DESTINATION lib
    )

    install(FILES
        ${PROJECT_LICENSE_FILES}
        DESTINATION share/3sx/licenses
    )
endif()

# ======================================
# Packaging
# ======================================

set(CPACK_PACKAGE_NAME "3SX")
set(CPACK_PACKAGE_VENDOR "CrowdedStreet")
# set(CPACK_PACKAGE_VERSION "1.0.0")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
else(UNIX)
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
