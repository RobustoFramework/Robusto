cmake_minimum_required(VERSION 3.16)

set(C6_FINAL_FIRMWARE
    "${CMAKE_CURRENT_LIST_DIR}/../c6_delegate/build/robusto_c6_delegate.bin"
    CACHE FILEPATH "Built final C6 application image")
set(C6_BOOTSTRAP_FIRMWARE
    "${CMAKE_CURRENT_LIST_DIR}/../c6_delegate/build-bootstrap/robusto_c6_delegate.bin"
    CACHE FILEPATH "Built factory-migration bootstrap C6 application image")
foreach(firmware IN ITEMS C6_FINAL_FIRMWARE C6_BOOTSTRAP_FIRMWARE)
if(NOT EXISTS "${${firmware}}")
    message(FATAL_ERROR
        "Build both C6 images first or set ${firmware} explicitly")
endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${C6_FINAL_FIRMWARE}" "${C6_BOOTSTRAP_FIRMWARE}")

set(C6_BOOTSTRAP_CONTAINER_OFFSET 0)
set(C6_BOOTSTRAP_OFFSET 64)
set(C6_FINAL_CONTAINER_OFFSET 0x60000)
math(EXPR C6_FINAL_OFFSET "${C6_FINAL_CONTAINER_OFFSET} + 64")
set(C6_FIRMWARE_PARTITION_SIZE 0x150000)
set(C6_FIRMWARE_PACKAGED "${CMAKE_CURRENT_BINARY_DIR}/robusto_c6_bundle.bin")
file(WRITE "${C6_FIRMWARE_PACKAGED}" "")
foreach(kind IN ITEMS BOOTSTRAP FINAL)
file(SIZE "${C6_${kind}_FIRMWARE}" C6_${kind}_SIZE)
math(EXPR C6_${kind}_END
     "${C6_${kind}_CONTAINER_OFFSET} + 64 + ${C6_${kind}_SIZE}")
if(kind STREQUAL "BOOTSTRAP" AND
   C6_BOOTSTRAP_END GREATER C6_FINAL_CONTAINER_OFFSET)
    message(FATAL_ERROR
        "Bootstrap C6 container overlaps final container offset")
elseif(kind STREQUAL "FINAL" AND
       C6_FINAL_END GREATER C6_FIRMWARE_PARTITION_SIZE)
    message(FATAL_ERROR
        "Final C6 container exceeds slave_fw partition")
endif()
file(SHA256 "${C6_${kind}_FIRMWARE}" C6_${kind}_SHA256)
set(container "${CMAKE_CURRENT_BINARY_DIR}/robusto_c6_${kind}.container.bin")
execute_process(
    COMMAND "${PYTHON}"
        "${CMAKE_CURRENT_LIST_DIR}/package_c6_firmware.py"
        --input "${C6_${kind}_FIRMWARE}"
        --output "${container}"
        --size "${C6_${kind}_SIZE}"
        --sha256 "${C6_${kind}_SHA256}"
        --bundle "${C6_FIRMWARE_PACKAGED}"
        --offset "${C6_${kind}_CONTAINER_OFFSET}"
    RESULT_VARIABLE package_result
    ERROR_VARIABLE package_error
)
if(NOT package_result EQUAL 0)
    message(FATAL_ERROR "Cannot package ${kind} C6 image: ${package_error}")
endif()
execute_process(
    COMMAND "${PYTHON}" -m esptool image-info "${C6_${kind}_FIRMWARE}"
    RESULT_VARIABLE image_info_result
    OUTPUT_VARIABLE image_info
    ERROR_VARIABLE image_info_error
)
if(NOT image_info_result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect ${kind} C6 image: ${image_info_error}")
endif()
string(REGEX MATCH "ELF file SHA256: [0-9a-fA-F]+"
    elf_sha_match "${image_info}")
string(REPLACE "ELF file SHA256: " "" C6_${kind}_ELF_SHA256 "${elf_sha_match}")
string(LENGTH "${C6_${kind}_ELF_SHA256}" elf_sha_length)
if(NOT elf_sha_length EQUAL 64)
    message(FATAL_ERROR "${kind} C6 image lacks a 32-byte ELF SHA-256")
endif()
endforeach()

foreach(hash_name IN ITEMS C6_BOOTSTRAP_SHA256 C6_BOOTSTRAP_ELF_SHA256
                           C6_FINAL_SHA256 C6_FINAL_ELF_SHA256)
    string(REGEX MATCHALL "[0-9a-fA-F][0-9a-fA-F]" hash_bytes
           "${${hash_name}}")
    set(${hash_name}_BYTES "")
    foreach(hash_byte IN LISTS hash_bytes)
        string(APPEND ${hash_name}_BYTES "0x${hash_byte}, ")
    endforeach()
endforeach()
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/main/c6_firmware_manifest.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/c6_firmware_manifest.h"
    @ONLY
)

set(EXTRA_COMPONENT_DIRS
    "../../../../components/robusto"
)
set(PROJECT_VER "robusto-c6-provisioning")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(robusto_c6_provisioning)

esptool_py_flash_to_partition(flash slave_fw "${C6_FIRMWARE_PACKAGED}")