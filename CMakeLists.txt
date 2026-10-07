cmake_minimum_required(VERSION 3.21)
project(fritzing-render LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Fritzing's sources are compiled in place from a fritzing-app checkout.
set(FRITZING_APP "${CMAKE_SOURCE_DIR}/../fritzing-app" CACHE PATH "fritzing-app checkout")
set(SVGPP_DIR "${CMAKE_SOURCE_DIR}/vendor/svgpp-1.3.1" CACHE PATH "svgpp 1.3.1 (header-only)")
get_filename_component(FRITZING_APP "${FRITZING_APP}" ABSOLUTE)
if(NOT EXISTS "${FRITZING_APP}/src/fsvgrenderer.cpp")
  message(FATAL_ERROR "No fritzing-app checkout at ${FRITZING_APP}; set -DFRITZING_APP=...")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Xml)
# QZipReader (Qt's private zip reader) unpacks .fzz files. Qt 6.10 moved
# private modules into their own packages; before that Qt6::CorePrivate
# comes with Core.
find_package(Qt6 QUIET COMPONENTS CorePrivate)
if(EMSCRIPTEN)
  # Boost is used for headers only; point at a folder holding just boost/,
  # so the host's other system headers stay out of the WebAssembly build.
  set(FR_BOOST_INCLUDE "" CACHE PATH "a folder with boost/ in it (headers only)")
  set(Boost_INCLUDE_DIRS "${FR_BOOST_INCLUDE}")
else()
  find_package(Boost REQUIRED)
endif()

set(FZ "${FRITZING_APP}/src")

# The self-contained part of Fritzing's SVG export pipeline: part SVG
# splitting and scaling (SvgFileSplitter), connector geometry
# (FSvgRenderer), SVG text helpers (TextUtils, GraphicsUtils) and what they
# include. Everything that pulls in the application (the part model, wires,
# the sketch canvas) is reimplemented in src/.
set(FRITZING_SOURCES
  ${FZ}/connectors/svgidlayer.cpp
  ${FZ}/debugdialog.cpp
  ${FZ}/fsvgrenderer.cpp
  ${FZ}/svg/svgfilesplitter.cpp
  ${FZ}/svg/svgpathgrammar.cpp
  ${FZ}/svg/svgpathlexer.cpp
  ${FZ}/svg/svgpathparser.cpp
  ${FZ}/svg/svgpathrunner.cpp
  ${FZ}/utils/fmessagebox.cpp
  ${FZ}/utils/graphicsutils.cpp
  ${FZ}/utils/misc.cpp
  ${FZ}/utils/textmetrics.cpp
  ${FZ}/utils/textutils.cpp
  ${FZ}/viewlayer.cpp
)

# Everything but main(), shared by the CLI and the tests.
add_library(frcore STATIC
  ${FRITZING_SOURCES}
  src/fzp.cpp
  src/partlib.cpp
  src/sketch.cpp
  src/fzz.cpp
  src/generated.cpp
  src/render.cpp
)

target_include_directories(frcore PUBLIC
  src
  ${FZ}
  ${SVGPP_DIR}/include
  ${Boost_INCLUDE_DIRS}
)


target_link_libraries(frcore PUBLIC
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Xml Qt6::CorePrivate
)

# Where the parts and Fritzing's resources are at run time: in the source
# tree, or in the web build's virtual file system.
if(EMSCRIPTEN)
  target_compile_definitions(frcore PUBLIC
    FR_VENDOR_DIR="/parts"
    FR_PARTS_DIR="/parts/fritzing-parts-extra"
    FR_FRITZING_APP="/fritzing-app"
  )
else()
  target_compile_definitions(frcore PUBLIC
    FR_VENDOR_DIR="${CMAKE_SOURCE_DIR}/vendor"
    FR_PARTS_DIR="${CMAKE_SOURCE_DIR}/libraries/fritzing-parts-extra"
    FR_FRITZING_APP="${FRITZING_APP}"
  )
endif()

if(EMSCRIPTEN)
  # The web build: a module the page calls (src/wasm.cpp), with Fritzing's
  # resources preloaded; parts are fetched by the page as sketches need them.
  set(STAGE "${CMAKE_BINARY_DIR}/preload")
  file(REMOVE_RECURSE "${STAGE}")
  foreach(item ratsnestcolors.xml properties.xml templates parts fonts)
    file(COPY "${FRITZING_APP}/resources/${item}" DESTINATION "${STAGE}/fritzing-app/resources")
  endforeach()
  # Fritzing's ground symbol draws from the parts library's svg folder.
  file(COPY "${CMAKE_SOURCE_DIR}/vendor/fritzing-parts/svg/core/schematic/ground.svg"
       DESTINATION "${STAGE}/parts/fritzing-parts/svg/core/schematic")
  qt_add_executable(fritzing-render-web src/wasm.cpp)
  target_link_libraries(fritzing-render-web PRIVATE frcore)
  target_link_options(fritzing-render-web PRIVATE
    -lembind
    -sALLOW_MEMORY_GROWTH=1
    -sEXPORTED_RUNTIME_METHODS=FS
    "--preload-file=${STAGE}@/"
  )
  return()
endif()

add_executable(fritzing-render src/main.cpp)
target_link_libraries(fritzing-render PRIVATE frcore)

enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(frtests tests/tst_render.cpp)
target_link_libraries(frtests PRIVATE frcore Qt6::Test)
target_compile_definitions(frtests PRIVATE FR_TESTS_DIR="${CMAKE_SOURCE_DIR}/tests")
add_test(NAME frtests COMMAND frtests)
set_tests_properties(frtests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=qt.qpa.fonts=false;QT_SVG_DEFAULT_OPTIONS=2")
