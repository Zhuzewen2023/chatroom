INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR})

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

SET(APPLICATION_DIR ${CMAKE_CURRENT_SOURCE_DIR}/application)

INCLUDE_DIRECTORIES(${APPLICATION_DIR}/api)
INCLUDE_DIRECTORIES(${APPLICATION_DIR}/base)
INCLUDE_DIRECTORIES(${APPLICATION_DIR}/mysql)
INCLUDE_DIRECTORIES(${APPLICATION_DIR}/service)

INCLUDE_DIRECTORIES(/usr/include/mysql)
INCLUDE_DIRECTORIES(/usr/include/jsoncpp)

AUX_SOURCE_DIRECTORY(${APPLICATION_DIR}/api API_SOURCE_LIST)
AUX_SOURCE_DIRECTORY(${APPLICATION_DIR}/base BASE_SOURCE_LIST)
AUX_SOURCE_DIRECTORY(${APPLICATION_DIR}/mysql MYSQL_SOURCE_LIST)
AUX_SOURCE_DIRECTORY(${APPLICATION_DIR}/service SERVICE_SOURCE_LIST)

if(ENABLE_RPC)
    ADD_EXECUTABLE(chatroom main.cc ${API_SOURCE_LIST} ${BASE_SOURCE_LIST}
        ${MYSQL_SOURCE_LIST} ${SERVICE_SOURCE_LIST})
else()
    ADD_EXECUTABLE(chatroom main.cc ${API_SOURCE_LIST} ${BASE_SOURCE_LIST}
    ${MYSQL_SOURCE_LIST} ${SERVICE_SOURCE_LIST})
endif()

# 当 ENABLE_RPC 为真时，配置 Protobuf 和 gRPC 依赖
# Find Protobuf installation
if(ENABLE_RPC) # 条件判断：若变量 ENABLE_RPC 为 ON/TRUE，则执行块内逻辑
    # Looks for protobuf-config.cmake file installed by Protobuf's cmake installation.
    # set 命令：定义/修改变量，这里将 protobuf_MODULE_COMPATIBLE 设为 TRUE
    set(protobuf_MODULE_COMPATIBLE TRUE)
    # 添加 gRPC 和 Protobuf 相关路径
     # 查找 Protobuf 库（REQUIRED 表示找不到则报错）
    find_package(Protobuf REQUIRED)
     # 查找 gRPC 库（CONFIG 表示优先使用库的 config.cmake 配置文件）
    find_package(gRPC CONFIG REQUIRED)
     # 输出 Protobuf 版本信息（STATUS 是消息级别，会显示在编译输出中）
    message(STATUS "Using protobuf ${Protobuf_VERSION}")
     # 定义 Protobuf 核心库的目标变量（protobuf::libprotobuf 是 Protobuf 导出的“导入目标”）
    set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
     # 定义 gRPC 反射库的目标变量（gRPC::grpc++_reflection 是 gRPC 导出的反射库目标）
    set(_REFLECTION gRPC::grpc++_reflection)
    # 判断是否为交叉编译（CMAKE_CROSSCOMPILING 是 CMake 内置变量，交叉编译时为 TRUE）
    if(CMAKE_CROSSCOMPILING)
        # 查找 protoc 编译器（Protobuf 的代码生成工具）
        find_program(_PROTOBUF_PROTOC protoc)# find_program：查找可执行程序，这里找 protoc，结果存到 _PROTOBUF_PROTOC
    else()
        # 非交叉编译时，直接用 Protobuf 目标的文件路径（生成器表达式）
        set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)# $<TARGET_FILE:目标> 是生成器表达式，编译时会替换为目标的实际路径
    endif()
    
    # Find gRPC installation
    # Looks for gRPCConfig.cmake file installed by gRPC's cmake installation.
    find_package(gRPC CONFIG REQUIRED)
    message(STATUS "Using gRPC ${gRPC_VERSION}")
    # 定义 gRPC C++ 核心库的目标变量
    set(_GRPC_GRPCPP gRPC::grpc++)
     # 交叉编译时查找 gRPC C++ 插件（用于生成 C++ 代码的工具）
    if(CMAKE_CROSSCOMPILING)
        find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin) # 查找 grpc_cpp_plugin 可执行程序
    else()
        # 非交叉编译时，用 gRPC 目标的文件路径（生成器表达式）
        set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
    endif()
endif()

# 根据RPC开关决定链接的库
if(ENABLE_RPC)
    TARGET_LINK_LIBRARIES(chatroom muduo_net jsoncpp 
        mysqlclient uuid ssl crypto
        ${_REFLECTION}
        ${_GRPC_GRPCPP}
        ${_PROTOBUF_LIBPROTOBUF})
else()
    TARGET_LINK_LIBRARIES(chatroom muduo_net jsoncpp 
        mysqlclient uuid ssl crypto)
endif()