# Smart Mixer — подключения

## Arduino UNO R4 Minima

| Пин Arduino | Назначение |
|---|---|
| `D2` | `CLK` KY-040 |
| `D3` | `DT` KY-040 |
| `D4` | `STEP` A4988 |
| `D5` | `DIR` A4988 |
| `D6` | `ENABLE` A4988 |
| `D7` | `TRIG/PWM` насоса 1 |
| `D8` | `SW` кнопки KY-040 |
| `D9` | верхний концевик |
| `D10` | `TRIG/PWM` насоса 2 |
| `A4` / `SDA` | OLED `SDA` |
| `A5` / `SCL` | OLED `SCL` |
| `5V` | питание логики |
| `GND` | общая земля |

## Питание

```text
12V + → VMOT A4988
12V + → VIN+ MOSFET-модулей
12V − → силовой GND A4988
12V − → VIN− MOSFET-модулей

12V + → VIN Arduino
12V − → GND Arduino
```

`GND` Arduino, A4988 и MOSFET-модулей должны быть общими.

В текущей тестовой сборке Arduino питается через `VIN` от источника `12V`.
Не подавать `12V` на пин `5V`.

## OLED SSD1306 128×64, I2C

```text
OLED VCC → Arduino 5V
OLED GND → Arduino GND
OLED SDA → Arduino SDA или A4
OLED SCK/SCL → Arduino SCL или A5
```

Адрес в коде: `0x3C`.

## Энкодер KY-040

```text
CLK → D2
DT  → D3
SW  → D8
VCC → 5V
GND → GND
```

Кнопка замыкает `SW` на `GND`.

## A4988

```text
STEP   → D4
DIR    → D5
ENABLE → D6
VDD    → 5V
GND    → общий GND
VMOT   → +12V
силовой GND → 12V−
```

```text
RESET + SLEEP → 5V
MS1 + MS2 + MS3 → GND  (полный шаг)
```

```text
1A + 1B → одна обмотка двигателя
2A + 2B → вторая обмотка двигателя
```

Конденсатор `100 мкФ`

```text
конденсатор + → VMOT
конденсатор − → силовой GND
```



## MOSFET-модуль и насос

Для каждого модуля с выводами `VIN+`, `VIN−`, `OUT+`, `OUT−`, `GND`,
`TRIG/PWM`:

```text
12V +       → VIN+
12V −       → VIN−
насос +     → OUT+
насос −     → OUT−
Arduino GND → GND
```

Управление:

```text
насос 1: TRIG/PWM → D7
насос 2: TRIG/PWM → D10
```

Защитный диод для каждого насоса подключается параллельно:

```text
катод, полоска → насос+
анод           → насос−
```

## Верхний концевик

Используется схема с нормально замкнутым контактом:

```text
COM → GND
NC  → D9
NO  → не подключать
```
