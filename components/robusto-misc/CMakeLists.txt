FILE(GLOB rob_sources 
"include/*.h"
"src/*.c"
"src/pubsub/*.c"
)

idf_component_register(
    SRCS ${rob_sources} 
    INCLUDE_DIRS . include 
    REQUIRES robusto-base robusto-network robusto-server
)
