# YoRadio application component for the mixed ESP-IDF build.
# Компонент приложения YoRadio для mixed-сборки ESP-IDF.
#
# Replaces the untracked pioarduino stub that did FILE(GLOB_RECURSE src/*.*)
# and registered no requirements. That stub is not the component contract.
# The translation units stay in this PlatformIO src tree; nothing is moved.
# Заменяет неотслеживаемый stub pioarduino с GLOB src/*.* без REQUIRES.
# Файлы остаются в дереве PlatformIO src, переноса нет.

# CONFIGURE_DEPENDS is rejected: IDF collects requirements in cmake script mode.
# CONFIGURE_DEPENDS здесь нельзя: IDF снимает requirements в режиме cmake -P.
file(GLOB_RECURSE app_sources
    "*.c"
    "*.cpp"
    "*.cc"
    "*.S")

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "src" "src/lvgl_ui"
    REQUIRES
        framework-arduinoespressif32
        esp_lcd
        esp_timer
        mbedtls
        freertos
        esp_hw_support
        heap
        driver
        lwip
        esp_wifi
        nvs_flash
        spi_flash
        esp_psram
        esp_system
        log
)

# Mixed CMake builds the app component; PlatformIO -D flags do not reach these TUs.
# Audio.cpp AAC / static-RX still requires this compile gate.
# Mixed CMake собирает app-компонент; флаги -D из PlatformIO сюда не доходят.
# AAC / static-RX в Audio.cpp по-прежнему требует этот compile-gate.
target_compile_definitions(${COMPONENT_LIB} PRIVATE YORADIO_IDF_C1_CONFIG=1)

# Mixed pioarduino generates embed assembly but does not link it, and the
# basename symbols do not match the objcopy names font_provider.cpp already uses.
# RENAME_TO keeps those names so the TTF assets stay the current production files.
# Mixed pioarduino генерирует embed-ассемблер, но не линкует его, а имена по
# basename не совпадают с objcopy-именами в font_provider.cpp.
# RENAME_TO сохраняет эти имена; сами TTF остаются текущими production-файлами.
target_add_binary_data(${COMPONENT_LIB}
    "src/lvgl_ui/fonts/yoradio_factory_font.ttf"
    BINARY
    RENAME_TO "src_src_lvgl_ui_fonts_yoradio_factory_font_ttf")
target_add_binary_data(${COMPONENT_LIB}
    "src/lvgl_ui/fonts/yoradio_tabler.ttf"
    BINARY
    RENAME_TO "src_src_lvgl_ui_fonts_yoradio_tabler_ttf")
