cmake_minimum_required(VERSION 3.10)

add_executable(client Core_Client/Src/main.c)

add_executable(server Core_Server/Src/main.c)

add_executable(tcp_client tcp_client/Src/main.c)

add_executable(tcp_server tcp_server/Src/main.c)

add_executable(tcp_n_serverr tcp_n_server/Src/main.c)