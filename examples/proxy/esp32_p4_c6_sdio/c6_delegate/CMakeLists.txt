cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "../../../../components/robusto"
)
option(ROBUSTO_C6_BOOTSTRAP "Build the factory-migration bootstrap identity" OFF)
if(ROBUSTO_C6_BOOTSTRAP)
    set(PROJECT_VER "robusto-c6-bootstrap")
else()
    set(PROJECT_VER "robusto-c6-delegate")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(robusto_c6_delegate)

if(NOT CONFIG_ROBUSTO_PROXY OR NOT CONFIG_ROBUSTO_PROXY_SERVER OR
   NOT CONFIG_ROBUSTO_PROXY_SERVICE_PUBSUB OR
   NOT CONFIG_ROBUSTO_PUBSUB_SERVER)
    message(FATAL_ERROR
        "C6 delegate requires the Robusto proxy server and local PubSub server")
endif()