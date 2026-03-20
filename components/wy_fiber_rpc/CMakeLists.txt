idf_component_register(
    SRCS "wy_fiber_rpc.c" "wy_fiber_invoice.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client json mbedtls
)
# molecule_reader.h from wyltek-embedded-builder (official Nervos C bindings)
if(DEFINED ENV{WYLTEK_BUILDER_PATH})
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "$ENV{WYLTEK_BUILDER_PATH}/src/ckb"
    )
else()
    target_include_directories(${COMPONENT_LIB} PRIVATE
        "/home/phill/workspace/wyltek-embedded-builder/src/ckb"
    )
endif()
