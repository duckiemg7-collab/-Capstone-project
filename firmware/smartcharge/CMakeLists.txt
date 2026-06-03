# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS
        "main.c"
        "power_meter.c"
        "display_lvgl.c"
        "screens/charge_test_screen.c"
        "wifi_mqtt.c"
    INCLUDE_DIRS
        "."
        "screens"
)
