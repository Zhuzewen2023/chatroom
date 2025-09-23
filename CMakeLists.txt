# 添加RPC功能开关选项, 默认是关闭的
option(ENABLE_RPC "Enable RPC functionality" OFF)

# 如果启用RPC，添加相关的编译宏定义
if(ENABLE_RPC)
    add_definitions(-DENABLE_RPC)
endif()

add_subdirectory(server)

#if(ENABLE_RPC)
#    add_subdirectory(logic)
#    add_subdirectory(job)
#endif()

# cpp客户端
#add_subdirectory(client)
# cpp客户端性能测试
#add_subdirectory(client_perf)