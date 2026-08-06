cmake_minimum_required(VERSION 3.16)
project(FPGAUpper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Dependencies ──
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg)
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
    sdk_test/sdk_test.cpp
    sdk_test/mock/mock_protocol_handler.cpp
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
    sdk_test/sdk_test.h
    sdk_test/mock/mock_protocol_handler.h
)

set(RESOURCES
    resources.qrc
)

# ── Executable ──
add_executable(fpga_upper ${SOURCES} ${HEADERS} ${RESOURCES})

target_include_directories(fpga_upper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include   # photon_camera.h (dllimport)
)

target_link_libraries(fpga_upper PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    ${OpenCV_LIBS}
)

# ── PhotonCameraSDK DLL (reuses host protocol/communication/utils sources) ──
add_subdirectory(sdk)

# 宿主以第三方身份链接并调用 PhotonCameraSDK.dll（模拟第三方用户使用）
target_link_libraries(fpga_upper PRIVATE PhotonCameraSDK)

# 部署：把 SDK DLL 复制到 exe 同级目录，运行时才能加载
add_custom_command(TARGET fpga_upper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:PhotonCameraSDK>
        $<TARGET_FILE_DIR:fpga_upper>/PhotonCameraSDK.dll
    COMMENT "Copying PhotonCameraSDK.dll next to fpga_upper.exe"
)

# ── Copy assets to build directory ──
add_custom_command(TARGET fpga_upper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:fpga_upper>/assets
)

# ── sdk_standalone_test: 第三方独立 SDK 模式验证（只 include photon_camera.h）──
# 独立模式由 SDK 内部自建唯一一套通信（Cam_OpenNetworkDevice/Mock），
# 测试进程不调用 Cam_AttachProtocolHandler。纯 C++17，不链接 Qt。
add_executable(sdk_standalone_test
    sdk_standalone_test/main.cpp
)

target_include_directories(sdk_standalone_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include   # photon_camera.h (dllimport)
)

if(MSVC)
    target_compile_options(sdk_standalone_test PRIVATE /utf-8)
endif()

target_link_libraries(sdk_standalone_test PRIVATE
    PhotonCameraSDK
)

# 部署：把 SDK DLL 复制到测试 exe 同级目录，运行时才能加载
add_custom_command(TARGET sdk_standalone_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:PhotonCameraSDK>
        $<TARGET_FILE_DIR:sdk_standalone_test>/PhotonCameraSDK.dll
    COMMENT "Copying PhotonCameraSDK.dll next to sdk_standalone_test.exe"
)

# ── Windows-specific: hide console + deploy Qt plugins ──
if(WIN32)
    # Hide the console window (remove if you want console output for debugging)
    # set_target_properties(fpga_upper PROPERTIES WIN32_EXECUTABLE TRUE)

    # Helper: generator expression for vcpkg plugin directory
    set(_QT_PLUGIN_DIR ${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET})
    set(_QT_PLUGIN_DEBUG_DIR ${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug)

    function(qt_deploy_plugin_subdir SUBDIR)
        set(DBG_DIR "${_QT_PLUGIN_DEBUG_DIR}/Qt6/plugins/${SUBDIR}")
        set(REL_DIR "${_QT_PLUGIN_DIR}/Qt6/plugins/${SUBDIR}")
        add_custom_command(TARGET fpga_upper POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<IF:$<CONFIG:Debug>,${DBG_DIR},${REL_DIR}>"
                "$<TARGET_FILE_DIR:fpga_upper>/${SUBDIR}"
            COMMENT "Deploying Qt ${SUBDIR} plugins for $<CONFIG>..."
        )
    endfunction()

    qt_deploy_plugin_subdir(platforms)
    qt_deploy_plugin_subdir(imageformats)

    # Qt6Svg DLL: imageformats/qsvg*.dll 插件依赖它，但 exe 未直接引用 Qt6Svg 符号
    # （链接器将其从导入表剔除），自动依赖部署不会带上 → 手动复制，否则 SVG 全部加载失败。
    add_custom_command(TARGET fpga_upper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${_QT_PLUGIN_DEBUG_DIR}/bin/Qt6Svgd.dll,${_QT_PLUGIN_DIR}/bin/Qt6Svg.dll>"
            "$<TARGET_FILE_DIR:fpga_upper>/$<IF:$<CONFIG:Debug>,Qt6Svgd.dll,Qt6Svg.dll>"
        COMMENT "Deploying Qt6Svg DLL for $<CONFIG>..."
    )

    # Warn if Qt6Svg DLL is missing from the output (link dependency)
    add_custom_command(TARGET fpga_upper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "--- Qt plugins deployed ---"
    )
endif()