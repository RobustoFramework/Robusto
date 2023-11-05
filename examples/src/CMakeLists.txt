FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/examples/*.*)
#FILE(GLOB espcam_sources ${CMAKE_SOURCE_DIR}/.pio/libdeps/ESP32cam/esp32-camera/target/*.*)
#FILE(GLOB_RECURSE espcam_sources_esp32 ${CMAKE_SOURCE_DIR}/.pio/libdeps/ESP32cam/esp32-camera/target/esp32/*.*)

idf_component_register(
    SRCS ${app_sources} ${espcam_sources} ${espcam_sources_esp32}
#    INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/.pio/libdeps/ESP32cam/esp32-camera/target/jpeg_include 
#        ${CMAKE_SOURCE_DIR}/.pio/libdeps/ESP32cam/esp32-camera/target/private_include
    REQUIRES robusto esp32-camera
)
