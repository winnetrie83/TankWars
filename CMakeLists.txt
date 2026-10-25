cmake_minimum_required(VERSION 3.21)
project(TankWars VERSION 0.3.3 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()
find_package(Qt6 QUIET COMPONENTS Multimedia)

qt_add_executable(TankWars
    src/main.cpp
    src/map.h src/map.cpp
    src/world.h src/world.cpp
    src/session.h src/session.cpp
    src/controls.h
    src/sounds.h src/sounds.cpp
    src/visibility.h
    src/canvas.h src/canvas.cpp
    src/editor.h src/editor.cpp
    src/mainwindow.h src/mainwindow.cpp
)
qt_add_resources(TankWars "tankwars_audio" PREFIX "/audio"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    FILES assets/shot.wav assets/hit.wav assets/blast.wav assets/capture.wav assets/click.wav
          assets/jeep_mg.wav assets/minigun.wav assets/cannon.wav assets/rocket.wav assets/rocket_blast.wav assets/mine_blast.wav
)
target_link_libraries(TankWars PRIVATE Qt6::Widgets Qt6::Network)
if(Qt6Multimedia_FOUND)
    target_link_libraries(TankWars PRIVATE Qt6::Multimedia)
    target_compile_definitions(TankWars PRIVATE TW_HAVE_MULTIMEDIA=1)
elseif(WIN32)
    target_link_libraries(TankWars PRIVATE winmm)
endif()
set_target_properties(TankWars PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
