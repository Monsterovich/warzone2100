function(WZ_CONVERT_PATH_TO_VCPKG_PATH_VAR _OUTPUTVAR _INPUTPATH)
	if(NOT CMAKE_HOST_WIN32)
		string(REPLACE ":" ";" _INPUTPATH "${_INPUTPATH}")
	endif()
	set(${_OUTPUTVAR} "${_INPUTPATH}" PARENT_SCOPE)
endfunction(WZ_CONVERT_PATH_TO_VCPKG_PATH_VAR)

if(DEFINED ENV{VCPKG_DEFAULT_TRIPLET} AND NOT DEFINED VCPKG_TARGET_TRIPLET)
  set(VCPKG_TARGET_TRIPLET "$ENV{VCPKG_DEFAULT_TRIPLET}" CACHE STRING "")
endif()
if(DEFINED ENV{VCPKG_DEFAULT_HOST_TRIPLET} AND NOT DEFINED VCPKG_HOST_TRIPLET)
  set(VCPKG_HOST_TRIPLET "$ENV{VCPKG_DEFAULT_HOST_TRIPLET}" CACHE STRING "")
endif()
if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
	if(DEFINED ENV{VCPKG_OVERLAY_TRIPLETS})
		WZ_CONVERT_PATH_TO_VCPKG_PATH_VAR(VCPKG_OVERLAY_TRIPLETS "$ENV{VCPKG_OVERLAY_TRIPLETS}")
	endif()
	set(_build_dir_overlay_triplets "${CMAKE_CURRENT_BINARY_DIR}/vcpkg_overlay_triplets")
	if(EXISTS "${_build_dir_overlay_triplets}" AND IS_DIRECTORY "${_build_dir_overlay_triplets}")
		list(APPEND VCPKG_OVERLAY_TRIPLETS "${_build_dir_overlay_triplets}")
	endif()
	unset(_build_dir_overlay_triplets)
	set(_ci_dir_overlay_triplets "${CMAKE_CURRENT_SOURCE_DIR}/.ci/vcpkg/overlay-triplets")
	if(EXISTS "${_ci_dir_overlay_triplets}" AND IS_DIRECTORY "${_ci_dir_overlay_triplets}")
		list(APPEND VCPKG_OVERLAY_TRIPLETS "${_ci_dir_overlay_triplets}")
	endif()
	unset(_ci_dir_overlay_triplets)
	if(DEFINED VCPKG_OVERLAY_TRIPLETS)
		set(VCPKG_OVERLAY_TRIPLETS "${VCPKG_OVERLAY_TRIPLETS}" CACHE STRING "")
	endif()
endif()
if(NOT DEFINED VCPKG_OVERLAY_PORTS OR VCPKG_OVERLAY_PORTS STREQUAL "")
	if(DEFINED ENV{VCPKG_OVERLAY_PORTS})
		WZ_CONVERT_PATH_TO_VCPKG_PATH_VAR(VCPKG_OVERLAY_PORTS "$ENV{VCPKG_OVERLAY_PORTS}")
	endif()
	set(_ci_dir_overlay_triplets "${CMAKE_CURRENT_SOURCE_DIR}/.ci/vcpkg/overlay-ports")
	if(EXISTS "${_ci_dir_overlay_triplets}" AND IS_DIRECTORY "${_ci_dir_overlay_triplets}")
		list(APPEND VCPKG_OVERLAY_PORTS "${_ci_dir_overlay_triplets}")
	endif()
	unset(_ci_dir_overlay_triplets)
	if(DEFINED VCPKG_OVERLAY_PORTS)
		set(VCPKG_OVERLAY_PORTS "${VCPKG_OVERLAY_PORTS}" CACHE STRING "")
	endif()
endif()

if(VCPKG_TARGET_TRIPLET MATCHES "wasm32-emscripten")
  if(NOT DEFINED VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.ci/cmake/toolchains/wasm32-emscripten.cmake")
  endif()
endif()
