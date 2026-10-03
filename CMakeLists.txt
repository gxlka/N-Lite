cmake_minimum_required(VERSION 3.20)
project(NLite LANGUAGES CXX)

add_executable(N-Lite WIN32 src/main.cpp)
target_compile_features(N-Lite PRIVATE cxx_std_17)
target_compile_definitions(N-Lite PRIVATE UNICODE _UNICODE)
target_link_libraries(N-Lite PRIVATE psapi shell32 advapi32 comctl32 winmm dwmapi shlwapi)
set_property(TARGET N-Lite PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
  target_compile_options(N-Lite PRIVATE /W4 /permissive- /utf-8 /O1 /GL /Gw /Gy)
  target_link_options(N-Lite PRIVATE /OPT:REF /OPT:ICF /LTCG)
endif()
