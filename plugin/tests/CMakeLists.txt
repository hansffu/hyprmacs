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

add_executable(hyprmacs_protocol_tests
  protocol_tests.cpp
  ../src/protocol.cpp
)

target_include_directories(hyprmacs_protocol_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(hyprmacs_protocol_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
  NAME hyprmacs_protocol_tests
  COMMAND hyprmacs_protocol_tests
)

add_executable(hyprmacs_workspace_manager_tests
  workspace_manager_tests.cpp
  ../src/workspace_manager.cpp
  ../src/client_registry.cpp
  ../src/client_classifier.cpp
)

target_include_directories(hyprmacs_workspace_manager_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(hyprmacs_workspace_manager_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
  NAME hyprmacs_workspace_manager_tests
  COMMAND hyprmacs_workspace_manager_tests
)

add_executable(hyprmacs_client_registry_tests
  client_registry_tests.cpp
  ../src/client_registry.cpp
  ../src/client_classifier.cpp
)

target_include_directories(hyprmacs_client_registry_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(hyprmacs_client_registry_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
  NAME hyprmacs_client_registry_tests
  COMMAND hyprmacs_client_registry_tests
)

add_executable(hyprmacs_ipc_server_tests
  ipc_server_tests.cpp
  ../src/ipc_server.cpp
  ../src/workspace_manager.cpp
  ../src/client_registry.cpp
  ../src/client_classifier.cpp
  ../src/protocol.cpp
)

target_include_directories(hyprmacs_ipc_server_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(hyprmacs_ipc_server_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
  NAME hyprmacs_ipc_server_tests
  COMMAND hyprmacs_ipc_server_tests
)
