cmake_minimum_required(VERSION 3.16)
project(TaskbarMagnify CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(TaskbarMagnify WIN32
  src/main.cpp src/taskbar.cpp src/icons.cpp app.rc)
target_link_libraries(TaskbarMagnify PRIVATE uiautomationcore ole32 oleaut32 user32 gdi32 shell32)
if(MSVC)
  target_link_options(TaskbarMagnify PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup)
  target_compile_options(TaskbarMagnify PRIVATE /O2 /W4)
else()
  target_link_options(TaskbarMagnify PRIVATE -municode -mwindows)
endif()

enable_testing()
add_executable(test_math tests/test_math.cpp)
target_include_directories(test_math PRIVATE src)
add_test(NAME math COMMAND test_math)
