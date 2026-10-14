cmake_minimum_required(VERSION 3.20)
project(LightNotes VERSION 3.1.0 LANGUAGES CXX)
add_executable(LightNotes WIN32 main.cpp glass.cpp tear.cpp book.cpp
  stack.h painting.h session.h startup.h storage.h shortcuts.h testhooks.h tasks.h panels.h geometry.h glass.h tear.h book.h)
target_compile_features(LightNotes PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(LightNotes PRIVATE /utf-8 /W4)
else()
  target_compile_options(LightNotes PRIVATE -Wall -Wextra -Werror)
endif()
target_link_libraries(LightNotes PRIVATE user32 gdi32 comctl32 dwmapi shell32 ole32 d3d11 dxgi msimg32 advapi32)
if(MINGW)
  target_link_options(LightNotes PRIVATE -municode)
endif()

add_custom_command(TARGET LightNotes POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/LightNotes.manifest $<TARGET_FILE_DIR:LightNotes>/LightNotes.manifest
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/LightNotes.ico $<TARGET_FILE_DIR:LightNotes>/LightNotes.ico)
