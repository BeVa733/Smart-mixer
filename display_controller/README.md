# Smart Mixer display controller

Прошивка для **Elecrow CrowPanel ESP32 7″ Basic, Rev 3** с экраном диагностики
UART. Она устанавливает сессию с Arduino, показывает состояние подключения,
RTT, счётчики ошибок и снимок состояния. Кнопка SEND PING запускает проверку
связи; касания и ответы PONG считаются отдельно. При потере связи старое
состояние Arduino скрывается. Физическая проверка новой версии ещё предстоит.

## Сборка

Из каталога `display_controller`:

```sh
~/.platformio/penv/bin/platformio run
```

## Загрузка

Отключить внешний UART от панели, подключить CrowPanel через USB-C и выполнить:

```sh
~/.platformio/penv/bin/platformio run --target upload
```

Если PlatformIO не определит порт автоматически, добавить параметр:

```sh
~/.platformio/penv/bin/platformio run --target upload --upload-port /dev/ttyUSB0
```

После загрузки выключить питание, подключить UART через согласователь к Arduino
со сборкой `uart_diagnostics`, затем включить обе платы. Использовать USB панели
для питания, не как одновременный канал отладки.

Просмотр сырых кадров при отключённом внешнем UART:

```sh
~/.platformio/penv/bin/platformio device monitor
```

Скорость — `19200` бод. На GPIO43/44 работает только Serial0; отладочные строки
и логи core отключены. Сообщения ROM при старте возможны и игнорируются парсером.
При исправном запуске параметры PSRAM/framebuffer сохраняются, ошибки VSYNC
видны на экране. Это уже не прежняя прошивка с текстовым логом запуска.

## Зафиксированная конфигурация

- ESP32-S3-WROOM-1-N4R8: 4 МБ Flash, 8 МБ OPI PSRAM;
- RGB TFT: 800×480;
- сенсор: GT911 на I²C;
- I²C: SDA `GPIO19`, SCL `GPIO20`;
- подсветка: `GPIO2`;
- сброс GT911 Rev 3 выполняется через PCA9557 по адресу `0x18`;
- LVGL `9.2.2`;
- RGB-драйвер: штатный `esp_lcd` из Arduino-ESP32;
- два полноэкранных RGB565 framebuffer в PSRAM с переключением по VSYNC;
- внутренний bounce buffer: 10 строк;
- pioarduino Espressif32 `55.03.38` / Arduino-ESP32 `3.3.8`.

Распиновка RGB и последовательность запуска PCA9557 взяты из официальных
материалов Elecrow для CrowPanel 7″ Rev 3. Полный демонстрационный проект
Elecrow в репозиторий не копировался.

Правила разработки и объяснение выбранной архитектуры находятся в
[`DISPLAY_LVGL_RULES.md`](DISPLAY_LVGL_RULES.md).

Правила подключения UART, разделения режимов USB и требований к коду:
[`UART_RULES.md`](UART_RULES.md).
