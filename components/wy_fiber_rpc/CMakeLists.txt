idf_component_register(
    SRCS "wy_fiber_rpc.c" "wy_fiber_invoice.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client cjson mbedtls
)
