idf_component_register(
    SRCS "wy_agentpay.c"
    INCLUDE_DIRS "include"
    REQUIRES wy_fiber_rpc wy_x402 esp_http_client cJSON
)
