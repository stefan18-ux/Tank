# TANK

A small turret built on an ESP32-S3 (N16R8). A joystick aims a stepper-driven turret,
an ultrasonic sensor watches for anything that gets close, and a buzzer sounds an
alarm. Everything is controlled through a menu on a 16x2 LCD.

## What it can do

- **Menu on the LCD.** Scroll with the joystick, press to open an option. The selected
  line blinks so you always know where you are.
- **Calibrate the turret.** Turn it by hand with the joystick until it points straight
  ahead, press, and that position becomes the new 0°.
- **Choose the rotation speed.** A 16-step bar on the LCD (`----------`) sets how fast
  the turret turns.
- **Shoot mode.** Aim the turret with the joystick, limited to ±80° from the center.
  The further you push, the faster it turns, up to the speed you picked.
- **Proximity alarm.** When something comes closer than 60 cm, the buzzer plays a
  "target lock" beep. Volume can be raised or lowered.
- **Signal menu.** A list of sounds to choose from (selecting a sound is not wired up
  yet).

## Hardware

| Part | Notes |
|---|---|
| ESP32-S3 N16R8 dev board | 16 MB flash, 8 MB PSRAM |
| 28BYJ-48 stepper + ULN2003 driver | powered from 5 V, not from the board's 3.3 V |
| Analog joystick (KY-023 style) | powered from 3.3 V |
| HC-SR04 ultrasonic sensor | ECHO goes through a 1 kΩ / 2 kΩ voltage divider (5 V → 3.3 V) |
| Passive buzzer | an active buzzer cannot play notes or change volume |
| 16x2 LCD (HD44780, 4-bit mode) | RW tied to GND |

### Pins

| Function | GPIO |
|---|---|
| Joystick X / Y / button | 15 / 7 / 6 |
| Stepper IN1 / IN2 / IN3 / IN4 | 19 / 18 / 5 / 17 |
| Ultrasonic TRIG / ECHO | 9 / 10 |
| Buzzer | 13 |
| LCD RS / EN / D4 / D5 / D6 / D7 | 35 / 36 / 37 / 38 / 39 / 40 |

> On the N16R8, GPIO 35–37 are also used by the internal PSRAM, and GPIO 19 is the
> native USB D- line. Upload and use Serial through the board's **UART/COM** port.

## Using it

1. Point the turret straight ahead and power on. **Don't touch the joystick** for the
   first second.
2. The LCD shows the menu. Move the stick **up/down** to scroll, **press** to select.
3. Menu options:
   - **Calibrate rotor**: push left/right to turn the turret (no limits here). Press
     when it's centered; that spot becomes 0° and you go back to the menu.
   - **Select signal**: scroll through the sound list. Press to go back.
   - **Rotation speed**: push left/right to shrink or grow the bar. Press to go back.
   - **Start shooting!**: push left/right to aim. The turret stops by itself at ±80°.
     Let go and it stays where it is. Press to go back to the menu; the turret
     returns to the center.

## How it works

### Tasks and cores

The ESP32-S3 has two cores, and the work is split between them so that slow parts
never make the motor stutter:

| Core | What runs there |
|---|---|
| 0 | `loop()`: joystick, menu, LCD, stepper; plus the buzzer timer callback |
| 1 | the ultrasonic sensor task |

The sensor task measures the distance every 60 ms and stores it in a
`std::atomic<float>`. `loop()` just reads that value, so the slow `pulseIn()` call
(up to 30 ms) never blocks the motor.

`loop()` is moved to core 0 with a build flag (see **Building**), and the core 0
watchdog is disabled with `disableCore0WDT()`, because `loop()` never sleeps.

### Classes

- **`Display`** owns the LCD and the menu state (`Menu`, `Calibration`, `Signal`,
  `Rotation`, `Shoot`). Each screen is a list of lines; the LCD shows two at a time.
  It only redraws when something changed (a "modified" flag); otherwise it only
  updates the blinking line. This avoids `lcd.clear()` flicker and keeps `loop()` fast.
- **`Joystick`** turns the analog stick into menu events (`Up`, `Down`, `Left`,
  `Right`, at most one every 150 ms) and also exposes the raw X value for smooth
  motor control.
- **`Executor`** drives the stepper with AccelStepper in `HALF4WIRE` mode (4096 steps
  per turn, so 80° ≈ 910 steps). One function handles both calibration and shooting:
  - how far the stick is pushed sets the speed (between a minimum and the speed
    chosen in the menu);
  - the direction sets the target: the ±80° edge in Shoot mode, or a point very far
    away in Calibration (no limit);
  - releasing the stick calls `stop()`, which brakes smoothly.
  AccelStepper's acceleration makes starts and stops soft. `motor.run()` is called on
  every pass of `loop()`.
- **`Sonar`** runs the ultrasonic sensor in its own FreeRTOS task on core 1.
- **`Player`** plays the "target lock" sound on the buzzer:
  - notes are timed by an `esp_timer`, so the rhythm stays exact no matter what
    `loop()` is doing;
  - the volume comes from the PWM duty cycle (50% is loudest). `louder()` and
    `quieter()` step through 10 levels, each doubling the duty; it starts at the
    quietest level.

### Button

The joystick button uses an interrupt (`FALLING` edge with `INPUT_PULLUP`, so pressed
means LOW), debounced in software.

## Building

PlatformIO with the **pioarduino** platform (Arduino-ESP32 core 3.x).

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
lib_deps = AccelStepper, LiquidCrystal
```


## Known limitations

- The 28BYJ-48 has no position sensor. The turret's position at power-on counts as 0°
  until you calibrate, and the calibration is lost when the power goes off.
- Above about 1000 steps/s the motor skips steps and loses its position, so the
  maximum speed is kept around 800.
- Choosing a sound in **Select signal** isn't implemented yet.
