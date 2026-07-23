idf_component_register(
    SRCS
        "app_main.c"
        "c6_factory_bootstrap.c"
    INCLUDE_DIRS
        "."
        "${CMAKE_BINARY_DIR}/generated"
    PRIV_REQUIRES
        robusto
        esp_event
        esp_partition
        esp_system
        freertos
        nvs_flash
)

    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_hosted_init")