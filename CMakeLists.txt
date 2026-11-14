cmake_minimum_required(VERSION 3.21)

project(SleepInBedPartner VERSION 1.11.0 LANGUAGES CXX)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "in-source builds are not allowed")
endif()

# CommonLibSSE-NG source tree (same one DeliriumGate builds against)
if(DEFINED ENV{CommonLibSSEPath_NG} AND IS_DIRECTORY "$ENV{CommonLibSSEPath_NG}")
	set(COMMONLIB_DIR "$ENV{CommonLibSSEPath_NG}")
else()
	set(COMMONLIB_DIR "C:/dllmaker/CommonLibSSE-NG")
endif()
message(STATUS "Using CommonLibSSE-NG from: ${COMMONLIB_DIR}")

set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_PATCH_SAFETY OFF CACHE BOOL "" FORCE)

add_subdirectory("${COMMONLIB_DIR}" CommonLibSSE EXCLUDE_FROM_ALL)

# MinHook: hooks on three functions inside SleepInBedSKSE.dll 1.4
find_package(minhook CONFIG REQUIRED)

# SimpleIni: header-only, reads and writes SleepInBedPartner.ini
find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)

add_library(${PROJECT_NAME} SHARED
	src/Main.cpp
	src/Settings.cpp
	src/Patch.cpp
	src/Menu.cpp
	src/Camera.cpp
	src/Eyes.cpp
	src/Door.cpp
	src/Weapons.cpp
	src/PCH.h
	src/Settings.h
	src/Patch.h
	src/Menu.h
	src/Camera.h
	src/Eyes.h
	src/Door.h
	src/Weapons.h
	src/SKSEMenuFramework.h
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_include_directories(${PROJECT_NAME}
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		"${SIMPLEINI_INCLUDE_DIRS}"
)

target_compile_definitions(${PROJECT_NAME}
	PRIVATE
		UNICODE
		_UNICODE
		NOMINMAX
		WIN32_LEAN_AND_MEAN
)

target_compile_options(${PROJECT_NAME}
	PRIVATE
		/permissive-
		/Zc:preprocessor
		/EHsc
		/utf-8
		/MP
		$<$<CONFIG:Release>:/Zi>
)

target_link_options(${PROJECT_NAME}
	PRIVATE
		$<$<CONFIG:Release>:/DEBUG:FULL /OPT:REF /OPT:ICF /INCREMENTAL:NO>
)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE minhook::minhook)

# No automatic copy: the finished DLL and PDB stay in build\Release\
