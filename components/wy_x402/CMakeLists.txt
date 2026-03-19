idf_component_register(
    SRCS "wy_x402.c"
    INCLUDE_DIRS "include"
    REQUIRES wy_fiber_rpc esp_http_client cJSON
)
