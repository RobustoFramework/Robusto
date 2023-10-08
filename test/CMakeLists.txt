FILE(GLOB_RECURSE test_sources *.*)

idf_component_register(
    SRCS ${test_sources}
    REQUIRES robusto-base robusto-server robusto-network  robusto-misc
)
