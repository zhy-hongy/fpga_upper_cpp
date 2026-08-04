# ── PhotonCameraSDK DLL ──
# 与宿主 EXE 同源编译 protocol/ communication/ utils/（ABI 兼容，见 CLAUDE.md 风险说明）。
# SDK 不创建第二套 UDP / ProtocolHandler / worker，只作为宿主通信链路的适配层 + 数据回调出口。

set(SDK_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/crc16.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/control_frame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/transport_frame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/upgrade_frame.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../communication/udp_worker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../communication/udp_data.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../communication/protocol_handler.cpp
)

set(SDK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/camera_device.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/data_dispatcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/callback_worker.cpp
)

add_library(PhotonCameraSDK SHARED ${SDK_COMMON_SOURCES} ${SDK_SOURCES})

target_compile_definitions(PhotonCameraSDK PRIVATE PHOTONSDK_EXPORTS)

if(MSVC)
    target_compile_options(PhotonCameraSDK PRIVATE /utf-8)
endif()

target_include_directories(PhotonCameraSDK PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(PhotonCameraSDK PRIVATE
    Qt6::Core
    Qt6::Network
    ${OpenCV_LIBS}
)
