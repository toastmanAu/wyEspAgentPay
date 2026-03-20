idf_component_register(
    SRCS "wy_agentpay.c"
    INCLUDE_DIRS "include"
    REQUIRES json wy_fiber_rpc wy_x402
)
