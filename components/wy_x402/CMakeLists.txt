idf_component_register(
    SRCS "wy_x402.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "$ENV{IDF_PATH}/components/json"
    REQUIRES esp_http_client json wy_fiber_rpc
)
