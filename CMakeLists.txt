cmake_minimum_required(VERSION 3.10)

project(minirl
  VERSION 1.0.0
  DESCRIPTION "Mini 'readline' library"
  LANGUAGES C
)

option(WITH_UTF8 "Enable UTF-8" ON)
if(WITH_UTF8)
  message(STATUS "Building with UTF-8 support")
  set(UTF8_SOURCE utf8.c utf8.h)
else()
  add_definitions(-DDISABLE_UTF8)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wmissing-prototypes -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -D_GNU_SOURCE")

add_library(minirl SHARED
  minirl.c 
  include/minirl.h 
  buffer.c
  buffer.h
  char.h
  io.h
  private.h
  key_binding.c
  key_binding.h
  utils.h
  ${UTF8_SOURCE}
)

target_include_directories(minirl 
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${PROJECT_BINARY_DIR}
)

add_custom_target(data DEPENDS utf8data.c)

add_custom_command(
  OUTPUT utf8data.c
  COMMAND python2 ${CMAKE_SOURCE_DIR}/utf8data.py > utf8data.c
  DEPENDS utf8data.py UnicodeData.txt EastAsianWidth.txt GraphemeBreakProperty.txt)

add_custom_command(
  OUTPUT UnicodeData.txt
  COMMAND curl -o UnicodeData.txt http://www.unicode.org/Public/UNIDATA/UnicodeData.txt)
add_custom_command(
  OUTPUT EastAsianWidth.txt
  COMMAND curl -o EastAsianWidth.txt http://www.unicode.org/Public/UNIDATA/EastAsianWidth.txt)
add_custom_command(
  OUTPUT GraphemeBreakProperty.txt
  COMMAND curl -o GraphemeBreakProperty.txt http://www.unicode.org/Public/UCD/latest/ucd/auxiliary/GraphemeBreakProperty.txt)


file(GLOB headers include/*.h)
install(FILES ${headers} DESTINATION include/minirl)
install(TARGETS minirl DESTINATION lib)
