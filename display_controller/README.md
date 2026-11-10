# Smart Mixer display controller

Минимальная прошивка для **Elecrow CrowPanel ESP32 7″ Basic, Rev 3**.
Она выводит тестовый экран LVGL с одной кнопкой. При каждом корректном касании
кнопка меняет цвет и увеличивает счётчик `Touches`.

## Сборка

Из каталога `display_controller`:

```sh
~/.platformio/penv/bin/platformio run
```

## Загрузка

Подключить CrowPanel через USB-C и выполнить:

```sh
~/.platformio/penv/bin/platformio run --target upload
```

Если PlatformIO не определит порт автоматически, добавить параметр:

```sh
~/.platformio/penv/bin/platformio run --target upload --upload-port /dev/ttyUSB0
```

Монитор порта:

```sh
~/.platformio/penv/bin/platformio device monitor
```

Скорость — `115200` бод.

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
