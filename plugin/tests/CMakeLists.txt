add_executable(hyprmacs_plugin_symbol_smoke
  plugin_symbol_smoke.cpp
)

set_target_properties(hyprmacs_plugin_symbol_smoke PROPERTIES
  OUTPUT_NAME "hyprmacs-plugin-symbol-smoke"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(hyprmacs_plugin_symbol_smoke PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(hyprmacs_plugin_symbol_smoke PRIVATE dl)
endif()

add_test(
  NAME hyprmacs_plugin_symbol_smoke
  COMMAND $<TARGET_FILE:hyprmacs_plugin_symbol_smoke> $<TARGET_FILE:hyprmacs>
)
