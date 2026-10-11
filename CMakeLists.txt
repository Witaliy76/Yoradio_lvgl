# YoRadio mixed Arduino + ESP-IDF project entry.
# Точка входа mixed-сборки YoRadio (Arduino + ESP-IDF).
#
# S01-1 keeps the PlatformIO source tree where it is. This file only satisfies
# the pioarduino ESP-IDF project contract. It does not select a board.
# S01-1 не переносит дерево исходников. Файл только выполняет контракт
# pioarduino/ESP-IDF и не выбирает плату.

cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(yoradio)
