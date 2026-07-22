cmake_minimum_required(VERSION 3.16)

set(C6_FIRMWARE
    "${CMAKE_CURRENT_LIST_DIR}/../c6_delegate/build/robusto_c6_delegate.bin"
    CACHE FILEPATH "Built C6 application image to provision")
if(NOT EXISTS "${C6_FIRMWARE}")
    message(FATAL_ERROR
        "Build c6_delegate first or set C6_FIRMWARE to its application image")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${C6_FIRMWARE}")

file(SIZE "${C6_FIRMWARE}" C6_FIRMWARE_SIZE)
file(SHA256 "${C6_FIRMWARE}" C6_FIRMWARE_SHA256)
set(C6_FIRMWARE_OFFSET 64)
set(C6_FIRMWARE_PACKAGED
    "${CMAKE_CURRENT_BINARY_DIR}/robusto_c6_delegate.container.bin")
execute_process(
    COMMAND "${PYTHON}"
        "${CMAKE_CURRENT_LIST_DIR}/package_c6_firmware.py"
        --input "${C6_FIRMWARE}"
        --output "${C6_FIRMWARE_PACKAGED}"
        --size "${C6_FIRMWARE_SIZE}"
        --sha256 "${C6_FIRMWARE_SHA256}"
    RESULT_VARIABLE package_result
    ERROR_VARIABLE package_error
)
if(NOT package_result EQUAL 0)
    message(FATAL_ERROR "Cannot package C6 image: ${package_error}")
endif()
execute_process(
    COMMAND "${PYTHON}" -m esptool image-info "${C6_FIRMWARE}"
    RESULT_VARIABLE image_info_result
    OUTPUT_VARIABLE image_info
    ERROR_VARIABLE image_info_error
)
if(NOT image_info_result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect C6 image: ${image_info_error}")
endif()
string(REGEX MATCH "ELF file SHA256: [0-9a-fA-F]+"
    elf_sha_match "${image_info}")
string(REPLACE "ELF file SHA256: " "" C6_ELF_SHA256 "${elf_sha_match}")
string(LENGTH "${C6_ELF_SHA256}" C6_ELF_SHA256_LENGTH)
if(NOT C6_ELF_SHA256_LENGTH EQUAL 64)
    message(FATAL_ERROR "C6 image does not contain a 32-byte ELF SHA-256")
endif()

foreach(hash_name IN ITEMS C6_FIRMWARE_SHA256 C6_ELF_SHA256)
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