cmake_minimum_required(VERSION 3.16)
project(system_monitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "AddressSanitizer (ASan) and ThreadSanitizer (TSan) cannot be enabled at the same time.")
endif()

set(SANITIZER_COMPILE_FLAGS "")
set(SANITIZER_LINK_FLAGS "")

if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer (ASan) is enabled.")
    list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=address -fno-omit-frame-pointer -g)
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=address)
endif()

if(ENABLE_TSAN)
    message(STATUS "ThreadSanitizer (TSan) is enabled.")
    list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=thread -g)
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=thread)
endif()

if(ENABLE_UBSAN)
    message(STATUS "UndefinedBehaviorSanitizer (UBSan) is enabled.")
    list(APPEND SANITIZER_COMPILE_FLAGS -fsanitize=undefined -fno-omit-frame-pointer -g)
    list(APPEND SANITIZER_LINK_FLAGS -fsanitize=undefined)
endif()


set(HARDENING_FLAGS_COMMON
    -Wall -Wformat -Wformat=2 -Wconversion -Wimplicit-fallthrough
    -Werror=format-security
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
    -D_GLIBCXX_ASSERTIONS
    -fstack-clash-protection -fstack-protector-strong
    -fPIE
)
set(HARDENING_LINKER_FLAGS
    -pie
    -Wl,-z,nodlopen
    -Wl,-z,noexecstack
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,--as-needed
    -Wl,--no-copy-dt-needed-entries
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    message(STATUS "GCC detected, adding GCC-specific hardening flags.")
    list(APPEND HARDENING_FLAGS_COMMON -Wtrampolines)
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12.0)
        message(STATUS "GCC >= 12, enabling -Wbidi-chars")
        list(APPEND HARDENING_FLAGS_COMMON -Wbidi-chars=any)
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13.0)
        message(STATUS "GCC >= 13, enabling -fstrict-flex-arrays=3")
        list(APPEND HARDENING_FLAGS_COMMON -fstrict-flex-arrays=3)
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15.0)
        message(STATUS "GCC >= 15, enabling -fzero-init-padding-bits=all")
        list(APPEND HARDENING_FLAGS_COMMON -fzero-init-padding-bits=all)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Clang detected, most hardening flags are compatible.")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    message(STATUS "x86_64 architecture detected, enabling Control-Flow Protection.")
    list(APPEND HARDENING_FLAGS_COMMON -fcf-protection=full)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    message(STATUS "aarch64 architecture detected, enabling Branch Protection.")
    list(APPEND HARDENING_FLAGS_COMMON -mbranch-protection=standard)
endif()
set(HARDENING_FLAGS_PRODUCTION
    -fno-delete-null-pointer-checks
    -fno-strict-overflow
    -fno-strict-aliasing
    -ftrivial-auto-var-init=zero
)


set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Charts Sql)
find_package(PkgConfig REQUIRED)

pkg_check_modules(PCAPPLUSPLUS REQUIRED IMPORTED_TARGET PcapPlusPlus)

add_executable(system_monitor
    log.cpp
    main.cpp
    main_window.cpp
    network_info.cpp
    data_collector.cpp
    database_manager.cpp
    draggable_chart_view.cpp
    dns_page.cpp
    dns_collector.cpp
)

target_compile_options(system_monitor PRIVATE
    ${HARDENING_FLAGS_COMMON}
    $<$<CONFIG:Release,RelWithDebInfo>:${HARDENING_FLAGS_PRODUCTION}>
)
target_link_options(system_monitor PRIVATE
    ${HARDENING_LINKER_FLAGS}
)

if(SANITIZER_COMPILE_FLAGS)
    target_compile_options(system_monitor PRIVATE ${SANITIZER_COMPILE_FLAGS})
    target_link_options(system_monitor PRIVATE ${SANITIZER_LINK_FLAGS})
endif()

target_include_directories(system_monitor PRIVATE
    third/spdlog/include
    ${PCAPPLUSPLUS_INCLUDE_DIRS}
)
target_link_directories(system_monitor PRIVATE
)
target_link_libraries(system_monitor PRIVATE
    Qt6::Widgets
    Qt6::Network 
    Qt6::Charts
    Qt6::Sql
    PkgConfig::PCAPPLUSPLUS
    pthread
)
set_target_properties(system_monitor PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
