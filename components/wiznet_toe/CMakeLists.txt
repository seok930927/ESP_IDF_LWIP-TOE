set(WIZNET_IOLIB_ROOT ${COMPONENT_DIR}/lib/ioLibrary_Driver)

set(WIZNET_TOE_SRCS)

if(CONFIG_WIZNET_TOE_ENABLE)
    list(APPEND WIZNET_TOE_SRCS
        "${WIZNET_IOLIB_ROOT}/Ethernet/socket.c"
        "${WIZNET_IOLIB_ROOT}/Ethernet/wizchip_conf.c"
        "${WIZNET_IOLIB_ROOT}/Ethernet/W5500/w5500.c"
        "src/wiznet_spi_shared.c"
        "src/wiznet_spi_port.c"
        "src/wiznet_toe.c"
        "src/toe_netif.c"
        "src/toe_dispatcher.c"
        "src/toe_socket.c"
        "src/toe_demo.c"
    )
else()
    list(APPEND WIZNET_TOE_SRCS
        "src/wiznet_toe_stub.c"
    )
endif()

idf_component_register(
    SRCS ${WIZNET_TOE_SRCS}
    INCLUDE_DIRS
        "include"
        "lib/ioLibrary_Driver/Ethernet"
        "lib/ioLibrary_Driver/Ethernet/W5500"
    REQUIRES driver esp_netif esp_eth esp_timer
)

if(CONFIG_WIZNET_TOE_ENABLE)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC _WIZCHIP_=W5500)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wno-error=misleading-indentation
        -Wno-error=unused-variable
        -Wno-error=unused-but-set-variable
        -Wno-error=missing-field-initializers
    )
endif()
