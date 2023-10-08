FILE(GLOB rob_sources 
"include/*.h"
"include/compat/*.h"
"src/*.c"
"src/queue/*.c" 
"src/memory/*.*" 
"src/concurrency/*.c" 
"src/repeater/*.*" 
)

idf_component_register(
    SRCS ${rob_sources} 
    INCLUDE_DIRS . include 
    REQUIRES robusto-base
)
