#ifndef _my_theme_h
#define _my_theme_h

/*
    Тематика цветных дисплеев
    DSP_ST7735, DSP_ST7789, DSP_ILI9341, DSP_GC9106, DSP_ILI9225, DSP_ST7789_240
    ***********************************************************************
    *    !!! Этот файл должен находиться в корневом каталоге скетча !!!    *
    ***********************************************************************
    Раскомментируйте (удалите двойную косую черту //) из нужной строки, чтобы применить цвет.
*/
#define ENABLE_THEME
#ifdef  ENABLE_THEME

/*----------------------------------------------------------------------------------------------------------------*/
/*       | COLORS             |   values (0-255)  |                                                               */
/*       | color name         |    R    G    B    |                                                               */
/*----------------------------------------------------------------------------------------------------------------*/
#define COLOR_BACKGROUND        0, 0,  0     /*  фон                                               */
#define COLOR_STATION_NAME        2, 84, 0     /*  Название станции                                              */
#define COLOR_STATION_BG          140, 159, 255     /*  Фон Названия станции                                   */
#define COLOR_STATION_FILL        140, 159, 255     /*  Фон заливки названия станции                              */
#define COLOR_SNG_TITLE_1         255, 255, 255     /*  Первое название                                               */
#define COLOR_SNG_TITLE_2         227,227,227     /*  Второе название                                             */
#define COLOR_WEATHER             255,   0, 216     /*  строка погоды                                            */
#define COLOR_VU_MAX              255, 0, 0     /*  max of VU meter - яркий красный */
#define COLOR_VU_MIN              41, 189, 207     /*  min of VU meter - яркий зеленый */
#define COLOR_CLOCK               41, 255, 55     /*  цвет часов                                               */
#define COLOR_CLOCK_BG            0,  0,   0     /*  цвет фона часов                                    */
#define COLOR_SECONDS             41, 255, 55     /*  цвет секунд (DSP_ST7789, DSP_ILI9341, DSP_ILI9225)      */
#define COLOR_DAY_OF_W            255,   255,   255     /*  цвет дня недели (DSP_ST7789, DSP_ILI9341, DSP_ILI9225)  */
#define COLOR_DATE                255,   255,   255     /*  цвет даты (DSP_ST7789, DSP_ILI9341, DSP_ILI9225)         */
#define COLOR_HEAP              255, 168, 162     /*  Строка кучи                                               */
#define COLOR_BUFFER            41, 255, 55     /*  buffer line                                               */
#define COLOR_IP                 41, 189, 207     /*  ip address                                                */
#define COLOR_VOLUME_VALUE      41, 189, 207     /*  Строка громкости (DSP_ST7789, DSP_ILI9341, DSP_ILI9225)      */
#define COLOR_RSSI              41, 189, 207     /*  rssi                                                      */
#define COLOR_VOLBAR_OUT        255, 255, 0     /*  контур шкалы громкости                                        */
#define COLOR_VOLBAR_IN         255, 236, 41     /*  заполнение шкалы громкости                                           */
//#define COLOR_DIGITS            100, 100, 255     /*  громкость/номер станции                                   */
#define COLOR_DIVIDER             255, 255, 0     /*  цвет разделителя (DSP_ST7789, DSP_ILI9341, DSP_ILI9225)      */
#define COLOR_BITRATE           255, 255, 0     /*  bitrate                                                   */
//#define COLOR_PL_CURRENT          0,   0,   0     /*  текущий элемент плейлиста                                     */
//#define COLOR_PL_CURRENT_BG      91, 118, 255     /*  фон текущего элемента плейлиста                          */
//#define COLOR_PL_CURRENT_FILL    91, 118, 255     /*  фон заливки текущего элемента плейлиста                     */
#define COLOR_PLAYLIST_0        255, 255, 255     /*  playlist string 0                                         */
#define COLOR_PLAYLIST_1          170, 170, 170     /*  playlist string 1                                         */
#define COLOR_PLAYLIST_2        140,   140, 140     /*  playlist string 2                                         */
#define COLOR_PLAYLIST_3          90,   90, 90     /*  playlist string 3                                         */
#define COLOR_PLAYLIST_4          60, 60, 60     /*  playlist string 4                                         */


#endif  /* #ifdef  ENABLE_THEME */
#endif  /* #define _my_theme_h  */
