idf_component_register(
    SRCS "wy_fiber_rpc.c" "wy_fiber_invoice.c"
    INCLUDE_DIRS
        "include"
        # molecule_reader.h from wyltek-embedded-builder (official Nervos C bindings)
        "$ENV{WYLTEK_BUILDER_PATH}/src/ckb"
    REQUIRES esp_http_client cjson mbedtls
)
# If WYLTEK_BUILDER_PATH not set, fall back to sibling path
if(NOT DEFINED ENV{WYLTEK_BUILDER_PATH})
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../../../wyltek-embedded-builder/src/ckb"
    )
endif()
