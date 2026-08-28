cmake_minimum_required(VERSION 3.20)

project(Ludora
    VERSION 0.1.0
    DESCRIPTION "Motor propio con ventana borderless escalable (Win32 + GDI)"
    LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Ludora es un motor Win32. Compila en Windows.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Salida separada por configuracion: Debug y Release no deben pisarse el .exe
# en la misma ruta (un build de Debug dejaria un binario de 2.4 MB donde se
# espera el de Release).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_SOURCE_DIR}/bin/debug")

add_executable(Ludora WIN32
    src/main.cpp
    src/core/Engine.cpp
    src/core/Clock.cpp
    src/platform/Window.cpp
    src/platform/Input.cpp
    src/render/Framebuffer.cpp
    src/render/RendererGDI.cpp
    src/app/DemoScene.cpp
    src/app/Font5x7.cpp
    src/app/SelfTest.cpp
    src/app/Screenshot.cpp
    src/app/Account.cpp
    src/app/Device.cpp
    src/app/Profile.cpp
    src/app/Vault.cpp
    src/app/Blake2b.cpp
    src/app/Argon2.cpp
    src/app/Session.cpp
    src/app/GameInstall.cpp
    src/app/HomeScene.cpp
    src/ui/TextField.cpp
    src/ui/TitleBar.cpp
    src/ui/ScrollPanel.cpp
    res/Ludora.rc
)

target_include_directories(Ludora PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(Ludora PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    $<$<CONFIG:Debug>:LUDORA_DEBUG>
)

target_link_libraries(Ludora PRIVATE user32 gdi32 shell32 dwmapi winmm)

if(MSVC)
    target_compile_options(Ludora PRIVATE /W4 /permissive- /utf-8
        $<$<CONFIG:Release>:/O2 /GL>)
    target_link_options(Ludora PRIVATE
        $<$<CONFIG:Release>:/LTCG>
        # Manifest: DPI por monitor v2 para que la escala sea real en pantallas HiDPI
        "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
    set_property(TARGET Ludora PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set_target_properties(Ludora PROPERTIES OUTPUT_NAME "Ludora")

# El manifest embebido controla el DPI awareness
target_sources(Ludora PRIVATE res/Ludora.manifest)
