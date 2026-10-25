find_package(Git REQUIRED)
set(halow_vendor_commit 2bb0d61a91ef288ac237afa481d6dabf11087031)
set(halow_vendor_relative examples/halow_spi_single/components/huge-ic-nonos-driver-v2.0.0-31919)
set(halow_vendor_root "${CMAKE_BINARY_DIR}/_deps/robusto_halow_vendor")
function(robusto_halow_git)
    execute_process(COMMAND "${GIT_EXECUTABLE}" ${ARGV}
                    RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "HaLow vendor git failed: ${error}")
    endif()
endfunction()
if(NOT EXISTS "${halow_vendor_root}/.git")
    robusto_halow_git(clone --filter=blob:none --no-checkout
        https://github.com/Xinyuan-LilyGO/T-Halow-P4.git "${halow_vendor_root}")
endif()
robusto_halow_git(-C "${halow_vendor_root}" sparse-checkout init --cone)
robusto_halow_git(-C "${halow_vendor_root}" sparse-checkout set "${halow_vendor_relative}")
robusto_halow_git(-C "${halow_vendor_root}" checkout --detach "${halow_vendor_commit}")
set(halow_vendor "${halow_vendor_root}/${halow_vendor_relative}")
set(halow_spi "${CMAKE_CURRENT_LIST_DIR}")
set(halow_spi_sources
    "${halow_spi}/halow_spi.c"
    "${halow_spi}/halow_spi_wire.c"
    "${halow_vendor}/hgic_raw.c"
    "${halow_vendor}/hgic_cmd.c"
    "${halow_vendor}/hgic_sdspi.c")
list(APPEND rob_sources ${halow_spi_sources})
list(APPEND rob_halow_private_includes "${halow_spi}" "${halow_vendor}")
set_source_files_properties(${halow_spi_sources} PROPERTIES
    COMPILE_DEFINITIONS raw_send=hgic_spi_raw_send)
set_source_files_properties("${halow_vendor}/hgic_sdspi.c" PROPERTIES
    COMPILE_OPTIONS "-Wno-error=missing-field-initializers")
