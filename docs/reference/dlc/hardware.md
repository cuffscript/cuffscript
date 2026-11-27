# `DLC:hardware`

```cuff
use DLC:hardware
```

The parts of the device a program runs on: its battery, buttons, lights, buzzer, screen, serial line, sensors and pins. The
engine reaches no hardware by itself. The program that embeds it, the **host**, gives each part it wants a script to have
([Giving a program hardware](../../guide/embedding.md#giving-a-program-hardware)), so what a function does on a laptop, a
washing machine or a small board is up to that host. **Nothing is given unless the host gives it**: a part that the host did
not give, or that the device does not have, is [`E4-044`](../errors.md#e4-044). A program finds out first with
`hw_supports`, or catches the error with `or_else`, and the same program then runs on a device with the part and on one without.

`cuffc` and `cuffsh` give no hardware unless they are started with `--hardware`; then they give what a desktop or laptop
computer has without a library: the battery and the temperature (on Linux; on Windows the battery), and a beep (on Windows the
speaker; elsewhere the bell of the terminal). See [what `--hardware` gives](#what---hardware-gives).

The numbers that name a button, a light, a line of a screen or a pin are the host's own, whole numbers from 0 to 65535
(`E4-025` otherwise). They are checked, like every argument, before the host is asked, so a wrong value is reported as wrong
on every device. A wrong type is `E4-005`, and the wrong number of arguments is `E4-010`. When a part fails while it is used
(a sensor that does not answer), the error is `E4-042`, as for any function the host gives.

## Functions

| Function | Result |
| :--- | :--- |
| `hw_supports(part)` | Whether the device has the part and the host gives it: `true` or `false`. `part` is one of `"battery"`, `"button"`, `"led"`, `"beep"`, `"screen_text"`, `"serial"`, `"sensor"`, `"pin"`, `"pwm"`, `"analog"` (`E4-025` otherwise). |
| `hw_battery()` | A map: `"level"`, how full the battery is from 0 to 100, and `"charging"`, whether it is being charged. |
| `hw_button(button)` | `true` while the button is held down. |
| `hw_led(light, on)` | Turns the light on (`true`) or off (`false`). |
| `hw_beep([hertz [, seconds]])` | Sounds the buzzer: 880 Hz for 0.2 seconds when nothing is given, and for 0.2 seconds when only `hertz` is. `hertz` is from 20 to 20000 and `seconds` from 0 to 10 (`E4-025`). Whether the call returns at once or when the sound ends is the device's. |
| `hw_screen_text(line, text)` | Shows `text` on a line of the device's screen, counted from 0. How many lines there are, and how much of a line shows, is the device's: a line it does not have is `E4-044`. |
| `hw_serial()` | What has arrived on the serial line since the last read, as a `str`; `""` when nothing has. It does not wait. |
| `hw_serial(text)` | Sends `text` on the serial line. Which line, and at what speed, is set by the host. |
| `hw_sensor(name)` | The reading of the sensor named `name`, a number. The names are the host's; the usual ones are `"temperature"` (degrees Celsius), `"humidity"` (percent), `"light"` (lux) and `"pressure"` (hectopascals). A sensor the device does not have is `E4-044`. |
| `hw_pin(pin)` | Reads a digital pin: `true` for high, `false` for low. |
| `hw_pin(pin, high)` | Sets a digital pin high (`true`) or low (`false`). |
| `hw_pwm(pin, duty)` | Drives the pin with a pulse that is on for `duty` of the time, from 0 to 1 (`E4-025`): a light's brightness, a motor's speed. |
| `hw_analog(pin)` | Reads an analog input as a fraction of its range, from 0 to 1, whatever the resolution of the device. |

A part can be there in one direction only: pins that can only be read, a serial line that can only send. `hw_supports` is then
`true`, and the other direction is `E4-044`, whose message says which way the part works.

```cuff
use DLC:hardware

func battery_text() do:
    if !hw_supports("battery") do:
        return "no battery"
    end
    map b = hw_battery()
    str level = to_str(b["level"]) + "%"
    if b["charging"] do:
        return level + ", charging"
    end
    return level
end

print(battery_text())
hw_led(0, true) or_else as Error e do:
    print(e.code, e.message)
end
```

```output
no battery
E4-044 hw_led() is not supported on this device
```

(This is what a program prints where the host gives no hardware, as `cuffc` without `--hardware`.)

## What `--hardware` gives

| Part | Linux | Windows | macOS |
| :--- | :--- | :--- | :--- |
| `battery` | `/sys/class/power_supply`: the first battery that tells its capacity; charging when its status is `Charging` | `GetSystemPowerStatus` | not given |
| `sensor` | `"temperature"`: the first thermal zone in `/sys/class/thermal` that gives a reading | not given | not given |
| `beep` | the terminal's bell (`/dev/tty`): its sound and length are the terminal's | the speaker, at the frequency and for the time asked | the terminal's bell |

A computer without a battery has `hw_supports("battery")` false, and so on. The other parts are not given: a computer has no
buttons, lights, pins or serial line that a script should drive without being told which, and its screen is the terminal that
`print` writes to.
