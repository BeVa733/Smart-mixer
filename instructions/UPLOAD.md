# Загрузка кода на Arduino

## Один раз на новом компьютере

1. Установить Arduino IDE 2.
2. Открыть `Tools → Board → Boards Manager`.
3. Найти `Arduino UNO R4 Boards` и установить пакет.
4. В `Sketch → Include Library → Manage Libraries` установить `U8g2`.

## Каждая загрузка

1. Подключить Arduino к компьютеру USB-кабелем с передачей данных.
2. Открыть файл `smart_mixer.ino`.
3. Выбрать плату:

   `Tools → Board → Arduino UNO R4 Boards → Arduino UNO R4 Minima`

4. Выбрать порт:

   `Tools → Port → /dev/ttyACM0` или появившийся порт Arduino.

6. Нажать `Upload`.
7. Для сообщений открыть `Tools → Serial Monitor`.
