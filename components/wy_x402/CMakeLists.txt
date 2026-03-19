idf_component_register(
    SRCS "wy_x402.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client cjson wy_fiber_rpc
)
