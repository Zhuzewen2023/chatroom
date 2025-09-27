cmake_minimum_required(VERSION 3.10)
project(chatroom)

# 添加RPC功能开关选项, 默认是关闭的
option(ENABLE_RPC "Enable RPC functionality" OFF)

# 如果启用RPC，添加相关的编译宏定义
if(ENABLE_RPC)
    add_definitions(-DENABLE_RPC)
endif()

# 配置可执行文件的输出目录（所有可执行文件都会放到 build/bin 下）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 配置库文件的输出目录（如果有动态库/静态库，会放到 build/lib 下）
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_subdirectory(server)

#if(ENABLE_RPC)
#    add_subdirectory(logic)
#    add_subdirectory(job)
#endif()

# cpp客户端
#add_subdirectory(client)
# cpp客户端性能测试
#add_subdirectory(client_perf)