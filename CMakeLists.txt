cmake_minimum_required(VERSION 3.24)

project( testsource )

if ( ${CMAKE_SYSTEM_NAME} MATCHES "Darwin" )
set(MACOSX TRUE)
find_package(OpenSSL)
else()
endif()

set( SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src )

set(CMAKE_BUILD_TYPE Debug)

file( GLOB SOURCES ${SOURCE_DIR}/*.cpp ${SOURCE_DIR}/*.c )

add_executable( testsource ${SOURCES} )

target_include_directories( testsource PUBLIC crankshaft/include )

target_link_directories( testsource PUBLIC crankshaft/lib )

if( MACOSX )
if( OPENSSL_FOUND ) 
target_include_directories( testsource PUBLIC ${OPENSSL_INCLUDE_DIR} )
target_link_libraries( testsource ${OPENSSL_LIBRARIES} )
target_link_libraries( testsource -lcrankshaft )
endif()
else()
target_link_libraries( testsource m ssl crypto pthread m z dl crankshaft )
endif()



