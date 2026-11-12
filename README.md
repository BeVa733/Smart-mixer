# Smart Mixer

Система автоматизированного приготовления напитков с двумя контроллерами.
Arduino UNO R4 Minima управляет шаговым приводом через TMC2209 и двумя
перистальтическими насосами 12 В через MOSFET-модули. Elecrow CrowPanel ESP32-S3
7″ Basic Rev3 выводит сенсорный интерфейс на LVGL.

Обычная сборка Arduino управляется через USB Serial. Для проверки связи есть
отдельная сборка `uart_diagnostics` без движения и включения насосов. CrowPanel
показывает UART-диагностику с кнопкой PING. Сессия, PING/STATUS и восстановление
связи реализованы и проверены на компьютере; стендовая проверка ещё предстоит.
Исполнительные UART-команды пока недоступны.

## Структура

До приезда CrowPanel интерфейс разрабатывается на ноутбуке в LVGL 9.2.2.
[Запуск и состояние ПК-версии](desktop_controller/README.md).
Общие экраны сохраняются для переноса на ESP32; текущая прошивка панели сохранена.

```text
motion_controller/          PlatformIO-проект Arduino UNO R4 Minima
  platformio.ini           плата, сборка, DFU-загрузка и USB Serial
  src/main.cpp             управление механизмами и Serial-команды
  include/desktop_recipes.hpp  USB-исполнитель слоёных рецептов в desktop_usb
  src/uart_diagnostics.cpp отдельная прошивка проверки UART без homing
  include/                 насосы, привод, энкодер, концевик и EEPROM
display_controller/         PlatformIO-проект CrowPanel ESP32-S3 Rev3
  platformio.ini           настройки ESP32-S3 и LVGL
  src/main.cpp             RGB-дисплей, GT911 и тестовый интерфейс
  include/lv_conf.h        конфигурация LVGL
desktop_controller/         LVGL/SDL-приложение Linux, USB-управление Arduino
shared/SmartMixerUi/        общие экраны LVGL для ПК и будущей интеграции CrowPanel
shared/SmartMixerControl/   модель приготовления со следованием штока уровню
docs/                       подключения, загрузка, калибровка и архитектура
shared/SmartMixerProtocol/  общий C++11-протокол, транспорт и диагностические сессии
tests/protocol/             тесты протокола на компьютере через CMake/CTest
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

# Отдельная UART-диагностика Arduino без homing
pio run -d motion_controller -e uart_diagnostics
pio run -d motion_controller -e uart_diagnostics -t upload

# CrowPanel ESP32-S3
pio run -d display_controller
pio run -d display_controller -t upload
pio device monitor -d display_controller
```

Если `pio` отсутствует в PATH, использовать
`~/.platformio/penv/bin/platformio` вместо `pio`.

## Документация

- [Подключения](docs/CONNECTIONS.md).
- [Правила подключения и программирования UART CrowPanel](display_controller/UART_RULES.md).
- [Загрузка и настройка портов](docs/UPLOAD.md).
- [Калибровка насосов и высоты штока](docs/CALIBRATION.md).
- [План архитектуры двух контроллеров](docs/DISPLAY_ARCHITECTURE.md).
- [Спецификация протокола v1](docs/PROTOCOL.md).
- [Этапы реализации протокола](docs/PROTOCOL_IMPLEMENTATION.md).
- [Тесты протокола](docs/PROTOCOL_TESTS.md).
- [Прошивка дисплея](display_controller/README.md).
- [Правила работы с RGB-дисплеем и LVGL](display_controller/DISPLAY_LVGL_RULES.md).

OLED SSD1306 и его программный модуль удалены. U8g2 для сборки не требуется.
