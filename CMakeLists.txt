cmake_minimum_required(VERSION 3.20)
# C и CXX: без "C" CMake молча пропустит miniz_tinfl.c (урок из SWBF2 Import).
project(BleachRoSImport LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MAX_VERSION "0" CACHE STRING "Год 3ds Max, например 2027")
set(3DSMAX_SDK_DIR "" CACHE PATH "Папка maxsdk")
option(BLEACH_BUILD_PLUGIN "Собрать плагин (нужен Max SDK)" OFF)
option(BLEACH_BUILD_TOOL   "Собрать bleachdump.exe (без SDK)" ON)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Нужна 64-битная сборка: cmake -A x64")
endif()

# /MD во всех конфигурациях: у Max и плагина одна куча.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
if(MSVC)
    add_compile_options(/guard:cf)
    add_link_options(/guard:cf)
    # std::mutex и старая msvcp140.dll в процессе Max
    add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
endif()

# ---- общий читатель форматов (без SDK) ----
add_library(tamcore STATIC
    src/tam_math.h
    src/tam_formats.h src/tam_formats.cpp
    src/tam_assemble.h src/tam_assemble.cpp
    src/tam_anim.h src/tam_anim.cpp
    src/bleach_core.h src/bleach_core.cpp
    src/lang.h src/lang.cpp src/lang_en.inc
    vendor/miniz/miniz_tinfl.c)
target_include_directories(tamcore PUBLIC src vendor/miniz)
if(MSVC)
    target_compile_options(tamcore PRIVATE /EHsc /utf-8 /W3)
    target_compile_definitions(tamcore PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_source_files_properties(vendor/miniz/miniz_tinfl.c PROPERTIES COMPILE_FLAGS "/W0")
    target_link_libraries(tamcore PUBLIC advapi32)
else()
    set_source_files_properties(vendor/miniz/miniz_tinfl.c PROPERTIES COMPILE_FLAGS "-w")
endif()

if(BLEACH_BUILD_TOOL)
    add_executable(bleachdump tools/bleachdump.cpp)
    target_link_libraries(bleachdump PRIVATE tamcore)
    if(MSVC)
        target_compile_options(bleachdump PRIVATE /EHsc /utf-8)
    endif()
    set_target_properties(bleachdump PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
                                                RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
endif()

if(BLEACH_BUILD_PLUGIN)
    if(NOT EXISTS "${3DSMAX_SDK_DIR}/include/max.h")
        message(FATAL_ERROR "Max SDK не найден: ${3DSMAX_SDK_DIR}")
    endif()
    add_library(BleachRoSImport SHARED
        src/bleach.h src/bleach_max.cpp src/bleach_dll.cpp
        src/bleach_ui.h src/bleach_ui.cpp
        src/bleach_res.h src/bleach.rc
        bleach.def)
    set_target_properties(BleachRoSImport PROPERTIES
        SUFFIX ".dlu" PREFIX "" OUTPUT_NAME "BleachRoSImport"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
    target_include_directories(BleachRoSImport PRIVATE src)
    target_include_directories(BleachRoSImport SYSTEM PRIVATE "${3DSMAX_SDK_DIR}/include")
    target_link_directories(BleachRoSImport PRIVATE "${3DSMAX_SDK_DIR}/lib/x64/Release")
    target_link_libraries(BleachRoSImport PRIVATE tamcore
        core.lib maxutil.lib bmm.lib geom.lib mesh.lib Maxscrpt.lib Paramblk2.lib
        comctl32.lib comdlg32.lib ole32.lib uuid.lib shell32.lib advapi32.lib)
    target_compile_definitions(BleachRoSImport PRIVATE
        _UNICODE UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
        # /Zp8 обязателен для Max SDK.
        target_compile_options(BleachRoSImport PRIVATE /EHsc /Zp8 /bigobj /utf-8 /Zc:__cplusplus /external:W0 /W3)
        target_link_options(BleachRoSImport PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    endif()
endif()

message(STATUS "BleachRoSImport: Max ${MAX_VERSION}, plugin=${BLEACH_BUILD_PLUGIN}, tool=${BLEACH_BUILD_TOOL}")
