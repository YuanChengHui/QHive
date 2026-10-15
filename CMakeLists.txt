cmake_minimum_required(VERSION 3.20)
project(QHive VERSION 1.0.0 LANGUAGES CXX)

# C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编码设置
if(MSVC)
    add_compile_options(/utf-8 /W4 /WX- /MP)
else()
    add_compile_options(-fexec-charset=UTF-8 -Wall -Wextra)
endif()

# Qt设置
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)

# 查找Qt
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS 
    Core 
    Gui 
    Widgets 
    Network
    Concurrent
    Sql
)

# ============================== 源文件 ==============================
set(SOURCES
    # 主入口
    src/main.cpp

    # core 模块 (应用协调、任务调度)
    src/core/HttpClient.cpp
    src/core/HttpClient.h
    src/core/DownloadTask.cpp
    src/core/DownloadTask.h
    src/core/DownloadTypes.h

    # network 模块 (纯网络 I/O)
    src/network/NetworkRequest.h
    src/network/HeadRequest.cpp
    src/network/HeadRequest.h
    src/network/NetworkContext.cpp
    src/network/NetworkContext.h
    src/network/NetworkContextPool.cpp
    src/network/NetworkContextPool.h
    src/network/DownloadRequest.cpp
    src/network/DownloadRequest.h

    # utils 模块 (文件写入、全局并发控制)
    src/utils/FileWriter.cpp
    src/utils/FileWriter.h
    src/utils/FileWriterPool.cpp
    src/utils/FileWriterPool.h
    src/utils/GlobalConcurrency.h

    # ui 模块
    src/ui/DownloadDialog.cpp
    src/ui/DownloadDialog.h
    src/ui/DownloadDialog.ui
    src/ui/DownloadTaskItemWidget.cpp
    src/ui/DownloadTaskItemWidget.h
    src/ui/DownloadTaskItemWidget.ui
    src/ui/QHive.cpp
    src/ui/QHive.h
    src/ui/QHive.ui

    # database 模块
    src/database/DatabaseManager.cpp
    src/database/DatabaseManager.h
    src/database/TaskRepository.cpp
    src/database/TaskRepository.h
)

# 资源文件
set(RESOURCES resources/resource.qrc)

# 可执行文件
qt_add_executable(${PROJECT_NAME} 
    ${SOURCES}
    ${RESOURCES}
)

# 包含目录
target_include_directories(${PROJECT_NAME} 
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/network
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
        ${CMAKE_CURRENT_SOURCE_DIR}/src/database
)

# 链接库
target_link_libraries(${PROJECT_NAME} 
    PRIVATE
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Network
        Qt::Concurrent
        Qt::Sql 
)

# Windows GUI应用
set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

# 安装规则
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)