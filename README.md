# Smart Mixer

Система автоматизированного приготовления напитков с двумя контроллерами.
Arduino UNO R4 Minima управляет шаговым приводом через TMC2209 и двумя
перистальтическими насосами 12 В через MOSFET-модули. Elecrow CrowPanel ESP32-S3
7″ Basic Rev3 выводит сенсорный интерфейс на LVGL.

Сейчас Arduino управляется через USB Serial, а CrowPanel запускает тестовый
экран с кнопкой. Обмен командами между контроллерами по UART ещё не реализован.

## Структура

```text
motion_controller/          PlatformIO-проект Arduino UNO R4 Minima
  platformio.ini           плата, сборка, DFU-загрузка и USB Serial
  src/main.cpp             управление механизмами и Serial-команды
  include/                 насосы, привод, энкодер, концевик и EEPROM
display_controller/         PlatformIO-проект CrowPanel ESP32-S3 Rev3
  platformio.ini           настройки ESP32-S3 и LVGL
  src/main.cpp             RGB-дисплей, GT911 и тестовый интерфейс
  include/lv_conf.h        конфигурация LVGL
docs/                       подключения, загрузка, калибровка и архитектура
smart_mixer.code-workspace  рабочая область VS Code/VSCodium для обеих прошивок
```

## Сборка и загрузка

Открыть `smart_mixer.code-workspace` и выбрать нужный проект в PlatformIO.
Из корня репозитория доступны те же операции через CLI:

```sh
# Arduino UNO R4 Minima
pio run -d motion_controller
pio run -d motion_controller -t upload
pio device monitor -d motion_controller

# CrowPanel ESP32-S3
pio run -d display_controller
pio run -d display_controller -t upload
pio device monitor -d display_controller
```

Если `pio` отсутствует в PATH, использовать
`~/.platformio/penv/bin/platformio` вместо `pio`.

## Документация

- [Подключения](docs/CONNECTIONS.md).
- [Загрузка и настройка портов](docs/UPLOAD.md).
- [Калибровка насосов и высоты штока](docs/CALIBRATION.md).
- [План архитектуры двух контроллеров](docs/DISPLAY_ARCHITECTURE.md).
- [Прошивка дисплея](display_controller/README.md).
- [Правила работы с RGB-дисплеем и LVGL](display_controller/DISPLAY_LVGL_RULES.md).

OLED SSD1306 и его программный модуль удалены. U8g2 для сборки не требуется.
