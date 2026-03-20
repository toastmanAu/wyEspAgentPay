idf_component_register(
    SRCS "wy_agentpay.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "$ENV{IDF_PATH}/components/json"
    REQUIRES json wy_fiber_rpc wy_x402
)
