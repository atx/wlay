cmake_minimum_required (VERSION 3.6)
project (wlay)

set (CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
find_package (Wayland REQUIRED)
find_package (WaylandScanner REQUIRED)
find_package (PkgConfig REQUIRED)
pkg_search_module (GLFW REQUIRED glfw3)
pkg_search_module (EPOXY REQUIRED epoxy)

option (WITH_ASAN "Enable ASan" OFF)

if (WITH_ASAN)
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
endif ()

ecm_add_wayland_client_protocol (
	WLR_OUTPUT_MANAGEMENT_SRC
	PROTOCOL wlr-protocols/unstable/wlr-output-management-unstable-v1.xml
	BASENAME wlr-output-management
)

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ggdb")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-unknown-pragmas -Wno-unused")

include_directories (nuklear/)
include_directories ("${CMAKE_BINARY_DIR}")

add_executable (wlay main.c ${WLR_OUTPUT_MANAGEMENT_SRC})
target_link_libraries (wlay ${GLFW_LIBRARIES} ${EPOXY_LIBRARIES} ${Wayland_LIBRARIES})

install (TARGETS wlay RUNTIME DESTINATION bin COMPONENT bin)
