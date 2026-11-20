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
# This reader is compiled into the fatfs library, where FILINFO is overridden.
# Этот reader собирается в библиотеку fatfs, где переопределён FILINFO.
list(FILTER app_sources EXCLUDE REGEX "fatfs_dir_read\\.c$")

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "src" "src/lvgl_ui"
    REQUIRES
        framework-arduinoespressif32
        esp_lcd
        esp_lcd_touch
        esp_lcd_touch_gt911
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
        fatfs
        sdmmc
)

# Mixed CMake builds the app component; PlatformIO -D flags do not reach these TUs.
# Audio.cpp AAC / static-RX still requires this compile gate.
# Mixed CMake собирает app-компонент; флаги -D из PlatformIO сюда не доходят.
# AAC / static-RX в Audio.cpp по-прежнему требует этот compile-gate.
target_compile_definitions(${COMPONENT_LIB} PRIVATE YORADIO_IDF_C1_CONFIG=1)

# HTTPS connects synchronously on loopTask; use the Arduino Core stack-size hook.
# HTTPS подключается синхронно на loopTask; размер задаёт штатный hook Arduino Core.
if("${CMAKE_BINARY_DIR}" MATCHES "/4848S040$")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE YORADIO_ARDUINO_LOOP_STACK_BYTES=16384)
endif()

# This component is compiled with the unscanned ninja rule, so a changed
# myoptions.h would not rebuild the TUs that include it. The user SD switch
# is one of those settings. One explicit edge covers the whole app component.
# Компонент собирается правилом ninja без сканера заголовков, поэтому правка
# myoptions.h сама не пересобирает включающие её файлы. Переключатель SD — одна
# из таких настроек. Одна явная связь покрывает весь компонент приложения.
foreach(_src ${app_sources})
    get_filename_component(_abs_src "${_src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
    set_property(SOURCE "${_abs_src}" APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/myoptions.h")
endforeach()

# Ordinary 4848S040 is the Storage Owner build. Directory detection covers mixed
# requirement collection, where PlatformIO -D flags may not be visible on app TUs.
# Штатный 4848S040 — сборка Storage Owner. Каталог учитывает mixed-сборку
# без флагов PIO на TU.
set(_yoradio_storage_owner_enabled OFF)
if("${CMAKE_BINARY_DIR}" MATCHES "/4848S040$")
    set(_yoradio_storage_owner_enabled ON)
elseif(YORADIO_STORAGE_OWNER_ENABLED)
    message(FATAL_ERROR "Storage Owner is only env 4848S040")
endif()
if(_yoradio_storage_owner_enabled)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE YORADIO_STORAGE_OWNER_ENABLED=1)
    # One symbol only. The fallback itself is still gated in storage_sdspi.cpp:
    # our mount, the myoptions policy, and ESP_ERR_TIMEOUT.
    # Ровно один символ. Сам fallback дополнительно закрыт в storage_sdspi.cpp:
    # наш mount, политика из myoptions и ESP_ERR_TIMEOUT.
    target_link_options(${COMPONENT_LIB} INTERFACE "LINKER:--wrap=sdmmc_init_sd_ssr")
    message(STATUS "YoRadio Storage Owner ENABLED for 4848S040")
    # IDF 5.5.5 hardcodes FF_LFN_BUF 255 next to ff.h. Quoted includes prefer
    # that sibling, so the fatfs sources are compiled from a build-tree copy
    # whose ffconf.h is the project wrapper. The installed SDK is not edited.
    # IDF 5.5.5 жёстко задаёт FF_LFN_BUF 255 рядом с ff.h. Кавычечный include
    # берёт этот соседний файл, поэтому исходники fatfs собираются из копии
    # в каталоге сборки с проектной обёрткой ffconf.h. Установленный SDK не правится.
    idf_component_get_property(_fatfs_lib fatfs COMPONENT_LIB)
    idf_component_get_property(_fatfs_dir fatfs COMPONENT_DIR)
    set(_lfn_repo "${CMAKE_CURRENT_LIST_DIR}/src/platform/storage/fatfs_lfn_override")
    set(_lfn_mirror "${CMAKE_BINARY_DIR}/yoradio_fatfs")
    # configure_file(COPYONLY) re-runs when an input changes, so mirror updates
    # without deleting the build directory. file(COPY) did not.
    # configure_file(COPYONLY) повторяет копирование при изменении входа —
    # зеркало обновляется без удаления каталога сборки. file(COPY) этого не делал.
    # vfs_fat_internal.h is copied too. It quote-includes esp_vfs_fat.h and
    # diskio_impl.h; #pragma once is per path, so the SDK copy would define
    # those types again. The installed sources and FS behavior stay the same.
    # vfs_fat_internal.h тоже копируется. Он кавычечно включает esp_vfs_fat.h
    # и diskio_impl.h; #pragma once различает пути, и копия из SDK определила
    # бы те же типы ещё раз. Установленные исходники и поведение FS те же.
    foreach(_rel src/ff.c src/ff.h src/ffunicode.c src/diskio.h
            diskio/diskio.c diskio/diskio_rawflash.c diskio/diskio_sdmmc.c diskio/diskio_wl.c diskio/diskio_impl.h
            vfs/vfs_fat_sdmmc.c vfs/vfs_fat_spiflash.c vfs/esp_vfs_fat.h vfs/vfs_fat_internal.h
            port/freertos/ffsystem.c)
        get_filename_component(_name "${_rel}" NAME)
        configure_file("${_fatfs_dir}/${_rel}" "${_lfn_mirror}/${_name}" COPYONLY)
    endforeach()
    configure_file("${_fatfs_dir}/src/ffconf.h" "${_lfn_mirror}/ffconf_sdk.h" COPYONLY)
    configure_file("${_lfn_repo}/ffconf.h" "${_lfn_mirror}/ffconf.h" COPYONLY)
    include("${_lfn_repo}/vfs_size_guard.cmake")
    configure_file("${_lfn_repo}/fatfs_dir_read.c" "${_lfn_mirror}/fatfs_dir_read.c" COPYONLY)
    configure_file("${_lfn_repo}/fatfs_dir_read.h" "${_lfn_mirror}/fatfs_dir_read.h" COPYONLY)
    get_target_property(_fatfs_srcs ${_fatfs_lib} SOURCES)
    set(_fatfs_replaced)
    foreach(_src ${_fatfs_srcs})
        get_filename_component(_name "${_src}" NAME)
        if(EXISTS "${_lfn_mirror}/${_name}")
            list(APPEND _fatfs_replaced "${_lfn_mirror}/${_name}")
        else()
            list(APPEND _fatfs_replaced "${_src}")
        endif()
    endforeach()
    list(APPEND _fatfs_replaced "${_lfn_mirror}/fatfs_dir_read.c")
    set_target_properties(${_fatfs_lib} PROPERTIES SOURCES "${_fatfs_replaced}")
    # App and Arduino must also use the mirrored FATFS/FIL/FSIZE_t declarations.
    # Приложение и Arduino также должны видеть типы FATFS/FIL/FSIZE_t из зеркала.
    target_include_directories(${_fatfs_lib} BEFORE PUBLIC "${_lfn_mirror}")
    message(STATUS "YoRadio FatFs: FAT/exFAT, 768-byte UTF-8 LFN buffer")
endif()

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
