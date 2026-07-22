cmake_minimum_required(VERSION 3.16)
project(FPGAUpper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Dependencies ──
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

# ── Sources ──
set(SOURCES
    main.cpp
    utils/config.cpp
    utils/logger.cpp
    protocol/crc16.cpp
    protocol/control_frame.cpp
    protocol/transport_frame.cpp
    protocol/upgrade_frame.cpp
    communication/udp_worker.cpp
    communication/udp_data.cpp
    communication/protocol_handler.cpp
    ui/widgets.cpp
    ui/protocol_analyzer.cpp
    ui/main_window.cpp
)

set(HEADERS
    utils/config.h
    utils/logger.h
    protocol/crc16.h
    protocol/control_frame.h
    protocol/camera_frame.h
    protocol/transport_frame.h
    protocol/upgrade_frame.h
    communication/udp_worker.h
    communication/udp_data.h
    communication/protocol_handler.h
    ui/widgets.h
    ui/protocol_analyzer.h
    ui/main_window.h
)

set(RESOURCES
    resources.qrc
)

# ── Executable ──
add_executable(fpga_upper ${SOURCES} ${HEADERS} ${RESOURCES})

target_include_directories(fpga_upper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(fpga_upper PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    ${OpenCV_LIBS}
)

# ── Copy assets to build directory ──
add_custom_command(TARGET fpga_upper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:fpga_upper>/assets
)

# ── Platform specific ──
if(WIN32)
    set_target_properties(fpga_upper PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
