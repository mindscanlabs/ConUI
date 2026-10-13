cmake_minimum_required(VERSION 3.15)

# macOS: the oldest system the bundle runs on. std::filesystem needs 10.15;
# 11.0 covers both Intel and Apple Silicon. Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(ConUI CXX)

# The macOS platform layer is Objective-C++ (src/Core/Platform_mac.mm).
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)

if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -ffunction-sections -fdata-sections -DNDEBUG")
    if(APPLE)
        # ld64 has no --gc-sections; -dead_strip is its equivalent, and stripping
        # is done on the finished bundle, not by the linker.
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-Wl,-dead_strip")
    else()
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s -Wl,--gc-sections")
    endif()
endif()

if(WIN32)
    set(RES_FILES "src/resources.rc")
endif()

# --- 1. System libraries ---------------------------------------------------
# GLFW is the only third-party library in the project. GLEW is deliberately
# absent: the renderer needs about thirty GL entry points and loads them by
# hand in src/Render/GLLoader.cpp (docs/DESIGN_SYSTEM.md §12).
find_package(glfw3  REQUIRED)
find_package(OpenGL REQUIRED)

# --- 2. Application --------------------------------------------------------
# Target name is ConUIApp, not the launcher name "ConUI" — see section 6
# (launcher) for why: this binary and its runtime DLLs live in bin/, and
# Windows resolves an exe's own directly-linked DLLs from its own directory
# before main() ever runs, so nothing this binary does at startup can
# retroactively redirect that search. Only a separate process that starts it
# can.
# --- 2a. Which application ------------------------------------------------
# The core is the same for every build; what differs is src/App/. The
# playground is the showcase of every widget; the skeleton is the empty
# template a fork starts from (docs/FORK.md). Pick with
#     cmake -DCONUI_APP=skeleton ..
set(CONUI_APP "playground" CACHE STRING "Application to build: playground or skeleton")
set_property(CACHE CONUI_APP PROPERTY STRINGS playground skeleton)
# The animated cat in the playground's ABOUT box: a demonstration of a
# picture an application draws itself (docs/DESIGN_SYSTEM.md §16), not part of
# the design system. Off: ABOUT shows its text only.
option(CONUI_ABOUT_CAT "Playground: the animated cat in the ABOUT box" ON)
if(CONUI_APP STREQUAL "playground")
    set(APP_SOURCES src/App/Playground.cpp src/App/ThemeEditor.cpp)
    set(APP_DEFINES CONUI_PLAYGROUND=1)
    if(CONUI_ABOUT_CAT)
        list(APPEND APP_SOURCES src/App/AboutCat.cpp)
        list(APPEND APP_DEFINES CONUI_ABOUT_CAT=1)
    endif()
    message(STATUS "ConUI ABOUT cat: ${CONUI_ABOUT_CAT}")
elseif(CONUI_APP STREQUAL "skeleton")
    set(APP_SOURCES src/App/Skeleton.cpp)
    set(APP_DEFINES CONUI_SKELETON=1)
else()
    message(FATAL_ERROR "CONUI_APP must be playground or skeleton, not ${CONUI_APP}")
endif()
message(STATUS "ConUI application: ${CONUI_APP}")

add_executable(ConUIApp
    src/main.cpp
    src/Core/Utils.cpp
    src/Core/ResourceManager.cpp
    src/Core/RasterStore.cpp
    src/Core/IconSheets.cpp
    src/Core/AppWindow.cpp
    src/Core/Platform_win.cpp
    src/Core/Platform_other.cpp
    src/UI/WindowChrome.cpp
    src/UI/Strings.cpp
    src/UI/Context.cpp
    src/UI/Layout.cpp
    src/UI/Widgets/Panel.cpp
    src/UI/Widgets/Label.cpp
    src/UI/Widgets/Button.cpp
    src/UI/Widgets/Toggle.cpp
    src/UI/Widgets/Progress.cpp
    src/UI/Widgets/Slider.cpp
    src/UI/Widgets/Scroll.cpp
    src/UI/Widgets/TextEdit.cpp
    src/UI/Widgets/TextInput.cpp
    src/UI/Widgets/TextArea.cpp
    src/UI/Widgets/Table.cpp
    src/UI/Widgets/EditTable.cpp
    src/UI/Widgets/Chart.cpp
    src/UI/Widgets/Diagram.cpp
    src/UI/Widgets/Tree.cpp
    src/UI/Widgets/Overlay.cpp
    src/UI/Widgets/Menu.cpp
    src/UI/Widgets/Tabs.cpp
    src/UI/Widgets/Sidebar.cpp
    src/UI/Widgets/Glitch.cpp
    src/Core/Json.cpp
    src/Core/ThemeStore.cpp
    src/Render/GLLoader.cpp
    src/Render/CellBuffer.cpp
    src/Render/FontAtlas.cpp
    src/Render/Renderer.cpp
    src/UI/Theme.cpp
    src/UI/Glyphs.cpp
    src/App/Scene.cpp
    src/App/SelfTest.cpp
    ${APP_SOURCES}
    ${RES_FILES}
)
# Windows: the app and its DLLs in bin/, the launcher next to the data files
# (section 6). Elsewhere the binary sits in the build folder itself, next to
# resources.dat and themes/, which is also how build_linux.sh stages it.
if(WIN32)
    set_target_properties(ConUIApp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
else()
    set_target_properties(ConUIApp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()

if(NOT MSVC)
    target_compile_options(ConUIApp PRIVATE -Wall -Wextra)
endif()
target_compile_definitions(ConUIApp PRIVATE ${APP_DEFINES})

target_include_directories(ConUIApp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ConUIApp
    PRIVATE
    glfw
    OpenGL::GL
)

# MinGW: the C++ runtime is linked in, so bin/ needs no libstdc++-6.dll or
# libgcc_s_seh-1.dll (build.sh verifies the closure). Clang on macOS has no
# such flags and does not need them.
if(MINGW)
    target_link_libraries(ConUIApp PRIVATE -static-libgcc -static-libstdc++)
endif()

# The macOS platform layer: Cocoa for NSWindow, QuartzCore for the backdrop
# (NSVisualEffectView). UNVERIFIED on hardware, like the whole macOS path.
if(APPLE)
    target_sources(ConUIApp PRIVATE src/Core/Platform_mac.mm)
    target_link_libraries(ConUIApp PRIVATE "-framework Cocoa" "-framework QuartzCore")
endif()

# The native frame lives on DWM: square corners, backdrop material and the
# attribute queries --probe reports (src/Core/Platform_win.cpp). dwmapi ships
# with every supported Windows, so this adds no extra runtime DLL.
if(WIN32)
    target_link_libraries(ConUIApp PRIVATE dwmapi)
endif()

if(WIN32)
    target_compile_definitions(ConUIApp PRIVATE GLFW_EXPOSE_NATIVE_WIN32)
    # GUI subsystem: double-clicking the app must not open a console window
    # beside it. --selftest, --probe and --dump still print from a terminal:
    # main() attaches to the parent console when there is one (src/main.cpp).
    set_target_properties(ConUIApp PROPERTIES WIN32_EXECUTABLE TRUE)
    get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
    # ConUIApp.exe's runtime DLL closure, resolved by hand via
    # `objdump -p build/bin/ConUIApp.exe | grep "DLL Name"` and walked
    # recursively. Windows-provided system DLLs are excluded. Re-run the
    # walk whenever a dependency is added.
    set(CONUI_RUNTIME_DLLS
        glfw3.dll
        libwinpthread-1.dll
    )
    foreach(_dll ${CONUI_RUNTIME_DLLS})
        add_custom_command(TARGET ConUIApp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MINGW_BIN_DIR}/${_dll}
            $<TARGET_FILE_DIR:ConUIApp>/${_dll}
        )
    endforeach()
endif()

# --- 3. Resource packing ---------------------------------------------------
add_executable(packer src/packer.cpp)
if(MINGW)
    target_link_libraries(packer PRIVATE -static-libgcc -static-libstdc++)
endif()

# Icon strips: PNG masks, one strip per pixel size — the caption boxes
# (tools/capicons.cpp) and the playground's sidebar sections
# (tools/secicons.cpp). Replace any with your own art of the same layout.
# The packer keys files by name alone, so names must be unique across folders.
file(GLOB ICON_STRIPS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/*/*.png)
set(RES_SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fonts/JetBrainsMono-Regular.ttf
    ${ICON_STRIPS}
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/resources.dat
    COMMAND packer ${CMAKE_CURRENT_BINARY_DIR}/resources.dat ${RES_SRC_FILES}
    DEPENDS packer ${RES_SRC_FILES}
    COMMENT "Packing resources into resources.dat"
    VERBATIM
)
add_custom_target(pack_resources ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/resources.dat)
add_dependencies(ConUIApp pack_resources)

# --- 4. Themes -------------------------------------------------------------
# themes/*.json ship alongside the executable. They are also written out at
# first run from the compiled-in copies, so a missing folder is not fatal
# (docs/DESIGN_SYSTEM.md §15) — this just seeds a fresh build directory.
file(GLOB CONUI_THEME_FILES ${CMAKE_CURRENT_SOURCE_DIR}/themes/*.json)
add_custom_target(copy_themes ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/themes
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CONUI_THEME_FILES}
            ${CMAKE_CURRENT_BINARY_DIR}/themes
    COMMENT "Staging themes/"
    VERBATIM
)
add_dependencies(ConUIApp copy_themes)

# --- 5. macOS app bundle ---------------------------------------------------
# UNVERIFIED on real hardware: only the Windows path has actually been run.
if(APPLE)
    set(MACOSX_BUNDLE_BUNDLE_NAME "ConUI")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "org.conui.playground")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0")
    set_target_properties(ConUIApp PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist.in
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    add_custom_command(TARGET ConUIApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_BINARY_DIR}/resources.dat
        $<TARGET_BUNDLE_CONTENT_DIR:ConUIApp>/Resources/resources.dat
    )
endif()

# --- 6. Launcher (Windows only) --------------------------------------------
# The exe a user actually runs, in the top-level folder next to
# resources.dat / data.bin / themes/. It sets CONUI_APP_ROOT (read by
# Core/Utils.cpp's GetExeDirectory()) and starts bin/ConUIApp.exe. Not needed
# on macOS: an .app bundle already separates Resources/ from Frameworks/.
if(WIN32)
    add_executable(ConUI WIN32 src/launcher.cpp ${RES_FILES})   # no console window
    add_dependencies(ConUI ConUIApp)
    target_link_libraries(ConUI PRIVATE -static-libgcc -static-libstdc++ -static)

    add_custom_command(TARGET ConUI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_BINARY_DIR}/resources.dat
        $<TARGET_FILE_DIR:ConUI>/resources.dat
    )
endif()
