idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES wy_agentpay esp_wifi nvs_flash
)
