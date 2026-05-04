idf_component_register(SRCS "main.c"
                            "ble_advertising.c"
                            "ble_scanning.c"
                            "display.c"
                            "qrcode.c"
                            "ui.c"
                            "buttons.c"
                            "met_tracker.c"
                            "leds.c"
                            "name_portal.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash bt driver esp_driver_rmt
                             esp_wifi esp_netif esp_event esp_http_server)
