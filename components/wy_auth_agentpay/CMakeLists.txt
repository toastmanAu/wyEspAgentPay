idf_component_register(
    SRCS "wy_auth_agentpay.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_http_client wy_fiber_rpc wy_agentpay cJSON
)
