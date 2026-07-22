cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "../../../../components/robusto"
)
set(PROJECT_VER "robusto-p4-proxy")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(robusto_p4_proxy_controller)

if(NOT CONFIG_ROBUSTO_PROXY OR NOT CONFIG_ROBUSTO_PROXY_CLIENT OR
   NOT CONFIG_ROBUSTO_PROXY_SERVICE_PUBSUB)
    message(FATAL_ERROR
        "P4 proxy requires the Robusto proxy client and PubSub service")
endif()