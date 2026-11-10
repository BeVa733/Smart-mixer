# Сборка и загрузка через PlatformIO

Установить расширение PlatformIO IDE для VS Code/VSCodium и открыть
`smart_mixer.code-workspace`. Оба контроллера имеют отдельные `platformio.ini`;
платформы и зависимости устанавливаются автоматически при первой сборке.

Команды ниже выполняются из корня репозитория. Если `pio` отсутствует в PATH,
использовать `~/.platformio/penv/bin/platformio`.

## Arduino UNO R4 Minima

Конфигурация: [`motion_controller/platformio.ini`](../motion_controller/platformio.ini).
Используются Renesas RA `1.9.0`, `board = uno_r4_minima`, Arduino framework и
штатная загрузка по DFU. Внешние библиотеки не требуются; EEPROM входит в core.

```sh
pio run -d motion_controller
pio run -d motion_controller -t upload
pio device monitor -d motion_controller
```

Порт по умолчанию — `/dev/ttyACM0`, скорость — `115200`, окончание строки — LF.
При другом номере устройства можно переопределить порт командой:

```sh
pio run -d motion_controller -t upload --upload-port /dev/ttyACM1
pio device monitor -d motion_controller --port /dev/ttyACM1
```

При загрузке PlatformIO переключает плату из USB Serial в DFU; порт может
временно исчезнуть. Если автоматический переход не сработал, дважды нажать
RESET на UNO R4 Minima и повторить загрузку.

После запуска Arduino автоматически ищет верхний концевик, двигая привод.
В мониторе команда `help` показывает справку, `status` — состояние,
`s` — останавливает насосы и калибровку. Полная процедура калибровки описана в
[`CALIBRATION.md`](CALIBRATION.md).

## CrowPanel ESP32-S3 Rev3

Конфигурация: [`display_controller/platformio.ini`](../display_controller/platformio.ini).
Порт по умолчанию — `/dev/ttyUSB0`, скорость — `115200`.

```sh
pio run -d display_controller
pio run -d display_controller -t upload
pio device monitor -d display_controller
```

Перед загрузкой закрыть последовательный монитор, занимающий порт.
Список подключённых устройств доступен через `pio device list`.
