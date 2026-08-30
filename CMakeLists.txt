cmake_minimum_required(VERSION 3.20)
project(sketch CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # clangd 自动发现 build/compile_commands.json

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Sanitizer：-DSKETCH_SANITIZER=address|thread|undefined|address,undefined
set(SKETCH_SANITIZER "" CACHE STRING "Enable sanitizer (address/thread/undefined)")
if(SKETCH_SANITIZER)
  add_compile_options(-fsanitize=${SKETCH_SANITIZER} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${SKETCH_SANITIZER})
endif()

find_package(Threads REQUIRED)

# ─── 分层（与目录结构一致：下层禁止 include 上层）─────────────────────
add_library(lpa_base INTERFACE) # base/：只依赖标准库
target_include_directories(lpa_base INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

function(sketch_add_executable target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE lpa_base Threads::Threads)
  target_compile_options(${target} PRIVATE -Wall -Wextra)
endfunction()

# ─── tests/：库的验证 ────────────────────────────────────────────────
sketch_add_executable(closure_smoke  tests/closure_smoke.cc)
sketch_add_executable(closure_micro  tests/closure_micro.cc)
sketch_add_executable(closure_stress tests/closure_stress.cc)

# ─── CTest：可自动判定的验证 ─────────────────────────────────────────
enable_testing()
add_test(NAME closure_smoke  COMMAND closure_smoke)
add_test(NAME closure_stress COMMAND closure_stress)
