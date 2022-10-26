cmake_minimum_required(VERSION 3.16)

project(minirl
  VERSION 1.0.0
  DESCRIPTION "Mini 'readline' library"
  LANGUAGES C
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wmissing-prototypes -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -D_GNU_SOURCE")

add_library(minirl SHARED
  minirl.c 
  include/minirl.h 
  buffer.c
  buffer.h
  io.h
  private.h
  key_binding.c
  key_binding.h
  utils.h
)

target_include_directories(minirl 
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${PROJECT_BINARY_DIR}
)

file(GLOB headers include/*.h)
install(FILES ${headers} DESTINATION include/minirl)
install(TARGETS minirl ARCHIVE DESTINATION lib)
