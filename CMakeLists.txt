cmake_minimum_required(VERSION 3.16)
project(s376-visual VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(S376_WITH_OSG "Build the OSG / osgEarth renderer" ON)
option(S376_WITH_GDAL "Load Copernicus GeoTIFF DEM/imagery through GDAL" ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
    # winsock2.h pulls windows.h, which otherwise #defines min/max and
    # breaks std::min / std::max (C2589).
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

set(S376_CORE_SRC
    src/vaxfloat.cpp
    src/log.cpp
    src/safety.cpp
    src/xt4.cpp
    src/config.cpp
    src/state.cpp
    src/host.cpp
    src/synth.cpp
    src/geo.cpp
    src/diagnostics.cpp
    src/render.cpp
    src/terrain.cpp
    src/dxt.cpp
    src/airport.cpp
    src/fetch.cpp
    src/imagery.cpp
    src/image.cpp
    src/display.cpp
    src/lights.cpp
    src/tod.cpp
    src/faa.cpp
    src/accuracy.cpp
    src/correct.cpp
    src/scenery.cpp
    src/parity.cpp
    src/repo_guard.cpp
    src/traffic.cpp
    src/landmarks.cpp
    src/missiles.cpp
    src/control.cpp
    src/wizard.cpp
    src/region.cpp
)

if(S376_WITH_GDAL)
    find_package(GDAL QUIET)
    if(GDAL_FOUND)
        add_compile_definitions(S376_HAS_GDAL)
        if(DEFINED GDAL_VERSION)
            message(STATUS "GDAL ${GDAL_VERSION} — GeoTIFF DEM/imagery enabled")
        else()
            message(STATUS "GDAL found — GeoTIFF DEM/imagery enabled")
        endif()
    else()
        message(STATUS "GDAL not found — portable .s376dem + synth DEM only")
    endif()
endif()

if(S376_WITH_OSG)
    find_package(OpenSceneGraph COMPONENTS osg osgDB osgGA osgViewer osgUtil osgText)
    if(OPENSCENEGRAPH_FOUND)
        add_compile_definitions(S376_HAS_OSG)
        list(APPEND S376_CORE_SRC src/render_osg.cpp)
        message(STATUS "OSG ${OPENSCENEGRAPH_VERSION} — renderer enabled")
        if(NOT osgEarth_FOUND AND EXISTS /opt/osgearth)
            list(APPEND CMAKE_PREFIX_PATH /opt/osgearth)
        endif()
        find_package(osgEarth QUIET)
        if(osgEarth_FOUND OR TARGET osgEarth OR TARGET osgEarth::osgEarth)
            add_compile_definitions(S376_HAS_OSGEARTH)
            message(STATUS "osgEarth found — streamed tiles enabled")
        else()
            message(STATUS "osgEarth not found — procedural globe (see docs/DESIGN.md)")
        endif()
    else()
        message(STATUS "OSG not found — headless core only")
    endif()
endif()

add_library(s376_core STATIC ${S376_CORE_SRC})
target_include_directories(s376_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(s376_core PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
if(WIN32)
    target_link_libraries(s376_core PUBLIC ws2_32 psapi)
endif()
if(OPENSCENEGRAPH_FOUND)
    target_include_directories(s376_core PUBLIC ${OPENSCENEGRAPH_INCLUDE_DIRS})
    target_link_libraries(s376_core PUBLIC ${OPENSCENEGRAPH_LIBRARIES})
    if(TARGET osgEarth::osgEarth)
        target_link_libraries(s376_core PUBLIC osgEarth::osgEarth)
    elseif(osgEarth_FOUND)
        target_link_libraries(s376_core PUBLIC osgEarth)
    endif()
    if(UNIX)
        target_link_libraries(s376_core PUBLIC X11)
    endif()
    find_package(OpenGL)
    if(OPENGL_FOUND)
        if(TARGET OpenGL::GL)
            target_link_libraries(s376_core PUBLIC OpenGL::GL)
        else()
            target_link_libraries(s376_core PUBLIC ${OPENGL_LIBRARIES})
        endif()
    endif()
endif()
if(GDAL_FOUND)
    if(TARGET GDAL::GDAL)
        target_link_libraries(s376_core PUBLIC GDAL::GDAL)
    else()
        target_include_directories(s376_core PUBLIC ${GDAL_INCLUDE_DIRS})
        target_link_libraries(s376_core PUBLIC ${GDAL_LIBRARIES})
    endif()
endif()

add_executable(s376-visual src/main.cpp)
target_link_libraries(s376-visual PRIVATE s376_core)

add_executable(s376-synth src/synth_main.cpp)
target_link_libraries(s376-synth PRIVATE s376_core)

add_executable(s376-doctor src/doctor_main.cpp)
target_link_libraries(s376-doctor PRIVATE s376_core)

add_executable(s376-cache src/cache_main.cpp)
target_link_libraries(s376-cache PRIVATE s376_core)

add_executable(s376-raster-parity tools/raster-parity/main.cpp)
target_link_libraries(s376-raster-parity PRIVATE s376_core)

# Git stamp for the wizard Info tab (and IG status).
set(S376_GIT_DESCRIBE "unknown")
set(S376_BUILD_DATE "")
execute_process(
    COMMAND git describe --always --tags --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE S376_GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
string(TIMESTAMP S376_BUILD_DATE "%Y-%m-%d")
target_compile_definitions(s376_core PUBLIC
    S376_GIT_DESCRIBE="${S376_GIT_DESCRIBE}"
    S376_BUILD_DATE="${S376_BUILD_DATE}"
)

add_executable(s376-wizard src/wizard_main.cpp)
target_link_libraries(s376-wizard PRIVATE s376_core)
set(S376_IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
find_package(OpenGL)
find_package(glfw3 QUIET)
if(NOT TARGET glfw AND NOT glfw3_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GLFW3 QUIET glfw3)
    endif()
endif()
if(EXISTS ${S376_IMGUI_DIR}/imgui.cpp AND OPENGL_FOUND AND
   (TARGET glfw OR glfw3_FOUND OR GLFW3_FOUND))
    target_sources(s376-wizard PRIVATE
        ${S376_IMGUI_DIR}/imgui.cpp
        ${S376_IMGUI_DIR}/imgui_draw.cpp
        ${S376_IMGUI_DIR}/imgui_tables.cpp
        ${S376_IMGUI_DIR}/imgui_widgets.cpp
        ${S376_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${S376_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(s376-wizard PRIVATE
        ${S376_IMGUI_DIR} ${S376_IMGUI_DIR}/backends)
    target_compile_definitions(s376-wizard PRIVATE S376_HAS_WIZARD_UI)
    if(TARGET glfw)
        target_link_libraries(s376-wizard PRIVATE glfw)
    elseif(glfw3_FOUND)
        target_link_libraries(s376-wizard PRIVATE glfw)
    else()
        target_include_directories(s376-wizard PRIVATE ${GLFW3_INCLUDE_DIRS})
        target_link_libraries(s376-wizard PRIVATE ${GLFW3_LIBRARIES})
    endif()
    if(TARGET OpenGL::GL)
        target_link_libraries(s376-wizard PRIVATE OpenGL::GL)
    else()
        target_link_libraries(s376-wizard PRIVATE ${OPENGL_LIBRARIES})
    endif()
    if(UNIX)
        target_link_libraries(s376-wizard PRIVATE dl)
    endif()
    message(STATUS "s376-wizard UI: Dear ImGui + GLFW")
else()
    message(STATUS "s376-wizard headless (no GLFW/ImGui)")
endif()

add_executable(s376-tests
    tests/test_main.cpp
    tests/test_vaxfloat.cpp
    tests/test_xt4.cpp
    tests/test_safety.cpp
    tests/test_config.cpp
    tests/test_geo.cpp
    tests/test_synth_loop.cpp
    tests/test_channels.cpp
    tests/test_terrain.cpp
    tests/test_airport.cpp
    tests/test_hat.cpp
    tests/test_fetch.cpp
    tests/test_imagery.cpp
    tests/test_diagnostics.cpp
    tests/test_image.cpp
    tests/test_display.cpp
    tests/test_lights.cpp
    tests/test_accuracy.cpp
    tests/test_faa_audit.cpp
    tests/test_correct.cpp
    tests/test_parity.cpp
    tests/test_repo_guard.cpp
    tests/test_traffic.cpp
    tests/test_landmarks.cpp
    tests/test_missiles.cpp
    tests/test_wizard.cpp
    tests/test_region.cpp
)
target_link_libraries(s376-tests PRIVATE s376_core)

enable_testing()
add_test(NAME unit COMMAND s376-tests)
set_tests_properties(unit PROPERTIES
    ENVIRONMENT "S376_ALLOW_SIM_LAN=;S376_ALLOW_LAN=;S376_LOG_DIR=${CMAKE_BINARY_DIR}/logs"
)
if(UNIX)
    add_test(NAME hangar-scripts COMMAND bash "${CMAKE_SOURCE_DIR}/tests/test_hangar_scripts.sh")
    set_tests_properties(hangar-scripts PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# Copy default config next to the binaries for out-of-tree runs.
file(COPY ${CMAKE_SOURCE_DIR}/config DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/data DESTINATION ${CMAKE_BINARY_DIR})

install(TARGETS s376-visual s376-cache s376-doctor s376-synth s376-raster-parity
        RUNTIME DESTINATION bin)
install(DIRECTORY config DESTINATION share/s376-visual)
install(DIRECTORY data DESTINATION share/s376-visual)

# ---- s376-fakehost: offline 737-300 host stand-in (loopback only) ----
add_library(s376_fakehost STATIC src/fakehost.cpp)
target_link_libraries(s376_fakehost PUBLIC s376_core)
target_link_libraries(s376-visual PRIVATE s376_fakehost)
if(UNIX)
    add_executable(s376-fakehost src/fakehost_main.cpp)
    target_link_libraries(s376-fakehost PRIVATE s376_fakehost)
    add_executable(s376-fakehost-tests tests/test_fakehost.cpp)
    target_link_libraries(s376-fakehost-tests PRIVATE s376_fakehost)
    add_test(NAME fakehost COMMAND s376-fakehost-tests $<TARGET_FILE:s376-visual>)
    set_tests_properties(fakehost PROPERTIES
        ENVIRONMENT "S376_ALLOW_SIM_LAN=;S376_ALLOW_LAN=;S376_LOG_DIR=${CMAKE_BINARY_DIR}/logs"
    )
    target_compile_definitions(s376-fakehost-tests PRIVATE S376_SRC_DIR="${CMAKE_SOURCE_DIR}")
    install(TARGETS s376-fakehost RUNTIME DESTINATION bin)
endif()
