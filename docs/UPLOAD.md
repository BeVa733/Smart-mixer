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

## Arduino для интерфейса ноутбука

```sh
pio run -d motion_controller -e desktop_usb
pio run -d motion_controller -e desktop_usb -t upload
```

В этой сборке homing при старте отключён: он запускается кнопкой интерфейса
или командой `home`. Насосы при старте выключены, двигатель включён на удержание.
Перед загрузкой закрыть приложение ПК, освободив порт. Запуск интерфейса:
`sh desktop_controller/run.sh`. Подробнее: [ПК-версия](../desktop_controller/README.md).
`uart_diagnostics` не подходит для управления механизмами.
Для приготовления по рецептам требуется текущая `desktop_usb` с `MIX_API 2`;
после изменения прошивки её нужно загрузить заново. Запуск разрешается после
подтверждения homing; EEPROM с калибровкой прошивка рецептов не изменяет.

## CrowPanel ESP32-S3 Rev3

Конфигурация: [`display_controller/platformio.ini`](../display_controller/platformio.ini).
Порт по умолчанию — `/dev/ttyUSB0`, скорость протокола/просмотра кадров — `19200`.
Отключить внешний UART перед загрузкой. После загрузки подключить его обратно
при выключенном питании; USB панели использовать для питания без передачи
отладочных данных. На UART нет прежнего текстового лога.

```sh
pio run -d display_controller
pio run -d display_controller -t upload
pio device monitor -d display_controller
```

Перед загрузкой закрыть последовательный монитор, занимающий порт.
Список подключённых устройств доступен через `pio device list`.

## Стенд UART без движения

Обычная Arduino-прошивка выше выполняет homing. Для первого теста связи нужна
отдельная диагностическая сборка, без механических команд и записи EEPROM:

```sh
pio run -d motion_controller -e uart_diagnostics
pio run -d motion_controller -e uart_diagnostics -t upload
pio device monitor -d motion_controller -e uart_diagnostics
```

В ней EN отключает драйвер: тестировать без нагрузки, которую двигатель должен
удерживать. После прошивки обеих плат при снятом питании подключить линии по
[UART_RULES.md](../display_controller/UART_RULES.md), затем включить питание.
На экране ожидаются CONNECTED / MOTION DIAG v1 и растущий PONG; при отключении
линии — потеря готовности, после восстановления — новая сессия. До проверки
стенда нельзя считать связь или отсутствие ряби подтверждёнными.
