idf_component_register(SRCS "src/soldered_bmm350.c"
                            "src/bmm350_api/bmm350.c"
                            "src/bmm350_api/bmm350_oor.c"
                    INCLUDE_DIRS "include" "include/bmm350_api"
                    REQUIRES esp_driver_i2c)
