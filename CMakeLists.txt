cmake_minimum_required(VERSION 3.6)
project(Minesweeper)

message("Source " "${CMAKE_SOURCE_DIR}")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(LIB "${CMAKE_SOURCE_DIR}/lib")
set(INCLUDE "${CMAKE_SOURCE_DIR}/include")

add_definitions(-DGLEW_STATIC -D_USE_MATH_DEFINES -DPERF)

find_package(OpenGL REQUIRED)

link_directories(${LIB})
include_directories(${OPENGL_INCLUDE_DIRS})
include_directories(${INCLUDE})

set(SOURCE_FILES main.cpp gl_includes.h Perf.h Perf.cpp stb_image_impl.cpp)
add_executable(Minesweeper ${SOURCE_FILES})

target_link_libraries(Minesweeper ${LIB}/libglew32.a)
target_link_libraries(Minesweeper ${LIB}/libglfw3.a)
target_link_libraries(Minesweeper ${OPENGL_LIBRARIES})
target_link_libraries(Minesweeper -static-libgcc -static-libstdc++)

file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR}/)
