cmake_minimum_required(VERSION 3.10)

project(tcp_test C) 


set(PROTOCOL_SOURCES
    Protocol/Src/tcp_protocol.c
    Protocol/Src/tcp_tlv.c
)

# 定义一个目标，用来持有所有公用的头文件路径，方便重用
add_library(Protocol_Includes INTERFACE)
target_include_directories(Protocol_Includes INTERFACE
    tcp_n_server/Inc
    tcp_client/Inc
    Protocol/Inc
)


add_executable(tcp_client
    tcp_client/Src/main.c
    ${PROTOCOL_SOURCES} 
)
target_link_libraries(tcp_client Protocol_Includes) 


add_executable(tcp_server
    tcp_server/Src/main.c
    ${PROTOCOL_SOURCES} 
)
target_link_libraries(tcp_server Protocol_Includes) 


add_executable(tcp_n_server
    tcp_n_server/Src/main.c
    ${PROTOCOL_SOURCES} 
)
target_link_libraries(tcp_n_server Protocol_Includes) 
