cmake_minimum_required(VERSION 3.16)
project(safevault CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# ─────────────────────────────────────────────
# SQLite3 amalgamation (bundled, no install needed)
# ─────────────────────────────────────────────
add_library(sqlite3_lib STATIC third_party/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC third_party)
if(UNIX)
    target_link_libraries(sqlite3_lib PUBLIC dl pthread)
endif()
# Silence warnings in the SQLite amalgamation
if(MSVC)
    target_compile_options(sqlite3_lib PRIVATE /W0)
else()
    target_compile_options(sqlite3_lib PRIVATE -w)
endif()

# ─────────────────────────────────────────────
# SafeVault executable
# ─────────────────────────────────────────────
add_executable(safevault
    src/main.cpp
    src/journal.cpp
    src/passthrough.cpp
    src/search.cpp
    src/watch.cpp
)

target_include_directories(safevault PRIVATE
    include
    third_party
)

target_link_libraries(safevault PRIVATE sqlite3_lib)

if(WIN32)
    target_link_libraries(safevault PRIVATE winhttp ws2_32)
    target_compile_definitions(safevault PRIVATE
        _WIN32_WINNT=0x0601   # Windows 7+
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(UNIX)
    target_link_libraries(safevault PRIVATE pthread)
endif()

# Treat warnings as useful but not errors during migration
if(MSVC)
    target_compile_options(safevault PRIVATE /W3 /wd4996)
else()
    target_compile_options(safevault PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
