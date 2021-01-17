cmake_minimum_required(VERSION 3.7...3.19)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

if(${CMAKE_VERSION} VERSION_LESS 3.12)
    cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

project(ShaderCompile CXX)

option(USE_DXC "Use DirectXShaderCompiler instead of D3DCompiler" ON)

option(RE2_BUILD_TESTING "" OFF)

add_subdirectory(shared/re2)
add_subdirectory(shared/gsl)
if(USE_DXC)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/shared/dxc
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()

    set(HLSL_OPTIONAL_PROJS_IN_DEFAULT OFF CACHE BOOL "" FORCE)
    set(HLSL_ENABLE_ANALYZE OFF CACHE BOOL "" FORCE)
    set(HLSL_OFFICIAL_BUILD ON CACHE BOOL "" FORCE)
    set(HLSL_ENABLE_FIXED_VER ON CACHE BOOL "" FORCE)
    set(HLSL_BUILD_DXILCONV OFF CACHE BOOL "" FORCE)
    set(ENABLE_SPIRV_CODEGEN ON CACHE BOOL "" FORCE)
    set(SPIRV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CLANG_ENABLE_ARCMT OFF CACHE BOOL "" FORCE)
    set(CLANG_ENABLE_STATIC_ANALYZER OFF CACHE BOOL "" FORCE)
    set(CLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
    set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
    set(HLSL_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
    set(LLVM_TARGETS_TO_BUILD "None" CACHE STRING "" FORCE)
    set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)
    set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LIBCLANG_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(LLVM_OPTIMIZED_TABLEGEN OFF CACHE BOOL "" FORCE)
    set(LLVM_APPEND_VC_REV OFF CACHE BOOL "" FORCE)
    set(LLVM_REQUIRES_EH ON CACHE BOOL "" FORCE)
    set(LLVM_ENABLE_EH ON CACHE BOOL "" FORCE)
    set(LLVM_REQUIRES_RTTI ON CACHE BOOL "" FORCE)
    set(LLVM_ENABLE_RTTI ON CACHE BOOL "" FORCE)
    set(LLVM_DEFAULT_TARGET_TRIPLE "dxil-ms-dx" CACHE STRING "" FORCE)
    set(CLANG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CLANG_CL OFF CACHE BOOL "" FORCE)
    set(DXC_BUILD_ARCH "x64" CACHE STRING "" FORCE)

    add_subdirectory(shared/dxc)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SRC
    ShaderCompile/cfgprocessor.cpp
    ShaderCompile/d3dxfxc.cpp
    ShaderCompile/ShaderCompile.cpp
    ShaderCompile/shaderparser.cpp
    ShaderCompile/utlbuffer.cpp
    )

add_executable(ShaderCompile ${SRC})
target_link_libraries(ShaderCompile PRIVATE re2::re2 Microsoft.GSL::GSL)
if (USE_DXC)
    target_link_libraries(ShaderCompile dxcompiler)
    target_compile_definitions(ShaderCompile PRIVATE USE_DXC)
    include_directories(shared/dxc/include)
endif()
include_directories(ShaderCompile/include shared/re2)

if (CMAKE_GENERATOR MATCHES "Visual Studio")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus")
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ShaderCompile)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus")
set_property(TARGET ShaderCompile PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(TARGET re2 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_definitions(ShaderCompile PRIVATE _ITERATOR_DEBUG_LEVEL=0)
target_compile_definitions(re2 PRIVATE _ITERATOR_DEBUG_LEVEL=0)
