FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/examples/*.*)

idf_component_register(
    SRCS ${app_sources}
    REQUIRES robusto
)
