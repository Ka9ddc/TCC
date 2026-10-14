idf_component_register(SRCS "softap_sta.c"
                    PRIV_REQUIRES esp_wifi nvs_flash
                    INCLUDE_DIRS "."
                    REQUIRES
                        driver
                        esp_wifi
                        esp_netif
                        nvs_flash
                        json
                        esp_http_server
                        esp_timer
                    )
