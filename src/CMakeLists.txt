cmake_minimum_required (VERSION 3.0)


add_library(client meshDetect.hpp client.hpp client.cpp clientOTA.cpp clientTime.cpp meshDetect.cpp)
target_include_directories(client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(client nlohmann_json)
target_link_libraries(client simple-web-server)
target_link_libraries(client cppcodec)

add_executable(painless-mesh-manager main.cpp)
target_link_libraries(painless-mesh-manager client)

# OPENSSL Start
if(APPLE)
    set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
endif()
find_package(OpenSSL REQUIRED)
target_link_libraries(client ${OPENSSL_LIBRARIES})
target_include_directories(client PUBLIC ${OPENSSL_INCLUDE_DIR})
# OPENSSL End

# Boost Start
find_package(Boost REQUIRED COMPONENTS filesystem thread)
include_directories(${Boost_INCLUDE_DIR})
target_link_libraries(painless-mesh-manager Boost::filesystem Boost::thread)
# Boost End

add_custom_command(TARGET painless-mesh-manager POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:painless-mesh-manager> ../../)




