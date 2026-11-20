#pragma once

#include "DLCCommon.h"
#include <stdexcept>
#include <type_traits>

// DLC:hardware: the parts of the device a program runs on: its battery, buttons, lights, buzzer, screen, serial port,
// sensors and pins. The engine reaches no hardware by itself. The host gives each part it wants the program to have as a
// function of its own (Engine::Options::hardware), and a part that it does not give is E4-044, which a program can catch
// or ask about first with hw_supports(). So hardware is off until a host turns a part on, and the same program runs on a
// device that has the part and on one that does not.

namespace cuff
{

    // What a hardware function of the host throws for a part this device does not have, or cannot use now: the program
    // gets E4-044 with the text. Any other exception is a failure of the part, E4-042, as from a host function.
    struct HardwareUnsupported : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct BatteryState
    {
        double level = 0;      // how full, from 0 to 100
        bool charging = false; // whether it is being charged now
    };

    // The parts of the device, each a function of the host. A part left empty is one the device does not have.
    // Numbers that name a button, a light or a pin are the host's own; they reach it as they were written (0 to 65535).
    struct Hardware
    {
        std::function<BatteryState()> battery;
        std::function<bool(int button)> button;                            // true while it is held down
        std::function<void(int led, bool on)> led;
        std::function<void(double hertz, double seconds)> beep;            // 20 to 20000 Hz, for up to 10 seconds
        std::function<void(int line, const std::string &text)> screenText; // lines from 0
        std::function<void(const std::string &text)> serialWrite;
        std::function<std::string()> serialRead;                           // what arrived since the last read, or ""
        std::function<double(const std::string &name)> sensor;             // by name: "temperature", "humidity", ...
        std::function<bool(int pin)> pinRead;                              // true for high
        std::function<void(int pin, bool high)> pinWrite;
        std::function<void(int pin, double duty)> pwm;                     // duty from 0 to 1
        std::function<double(int pin)> analog;                             // from 0 to 1 of the input's range
        // Whether a part is there after all ("battery", "serial", ...): asked by hw_supports() for a part that has a
        // function above, for a host that knows only when it looks (a laptop with its battery taken out). Optional.
        std::function<bool(const std::string &part)> supports;
    };

    struct HardwareDLCOptions
    {
        const Hardware *hardware = nullptr; // the engine's; outlives the library's functions
    };

    namespace hw
    {

        // Every hw_ function is one of these, so std::function needs one handler for all of them.
        struct Call
        {
            const Hardware *parts;
            Value (*fn)(const Hardware &, std::vector<Value> &, const SourceLocation &);
            Value operator()(std::vector<Value> &args, const SourceLocation &loc) const { return fn(*parts, args, loc); }
        };

        [[noreturn]] CUFF_COLD inline void unsupported(const char *fn, const char *part, const std::string &why, const SourceLocation &loc)
        {
            throw CuffRuntimeError(ErrorCode::HardwareUnsupported,
                                   why.empty() ? std::string(fn) + "() is not supported on this device" : std::string(fn) + "(): " + why, loc,
                                   std::string("ask hw_supports(\"") + part + "\") first, or catch the error with or_else");
        }

        [[noreturn]] CUFF_COLD inline void failed(const char *fn, const std::string &why, const SourceLocation &loc)
        {
            throw CuffRuntimeError(ErrorCode::HostFunctionFailed, std::string(fn) + "() failed: " + why, loc);
        }

        // Runs `call(context)`, a function of the host, turning what it throws into the program's error. One body for
        // every hw_ function, so the engine has one set of handlers for them, not one per call.
        CUFF_NOINLINE inline void guarded(const char *fn, const char *part, void (*call)(void *), void *context, const SourceLocation &loc)
        {
            try
            {
                call(context);
            }
            catch (const HardwareUnsupported &e)
            {
                unsupported(fn, part, e.what(), loc);
            }
            catch (const CuffError &)
            {
                throw;
            }
            catch (const std::bad_alloc &)
            {
                throw;
            }
            catch (const std::exception &e)
            {
                failed(fn, e.what(), loc);
            }
            catch (...)
            {
                failed(fn, "the host reported an unknown failure", loc);
            }
        }

        template <typename F>
        auto host(const char *fn, const char *part, const F &f, const SourceLocation &loc) -> decltype(f())
        {
            using R = decltype(f());
            if constexpr (std::is_void_v<R>)
            {
                guarded(fn, part, [](void *c) { (*static_cast<const F *>(c))(); }, const_cast<F *>(&f), loc);
            }
            else
            {
                struct Box
                {
                    const F &f;
                    R result{};
                } box{f};
                guarded(fn, part, [](void *c) { auto &b = *static_cast<Box *>(c); b.result = b.f(); }, &box, loc);
                return std::move(box.result);
            }
        }

        // A number that names a button, a light, a line or a pin.
        CUFF_NOINLINE inline int idArg(const char *fn, std::vector<Value> &args, size_t i, const char *what, const SourceLocation &loc)
        {
            const double d = expectNumber(fn, args, i, loc);
            if (!(d >= 0 && d <= 65535) || d != std::floor(d))
                throw ValueError(std::string(fn) + "() expects " + what + " to be a whole number from 0 to 65535, got " + formatCuffNumber(d), loc);
            return static_cast<int>(d);
        }

        CUFF_NOINLINE inline double rangeArg(const char *fn, std::vector<Value> &args, size_t i, const char *what, double lo, double hi,
                                             const SourceLocation &loc)
        {
            const double d = expectNumber(fn, args, i, loc);
            if (!(d >= lo && d <= hi))
                throw ValueError(std::string(fn) + "() expects " + what + " from " + formatCuffNumber(lo) + " to " + formatCuffNumber(hi) + ", got " +
                                     formatCuffNumber(d),
                                 loc);
            return d;
        }

        inline bool boolArg(const char *fn, std::vector<Value> &args, size_t i, const SourceLocation &loc)
        {
            if (!args[i].isBool())
                throw TypeError(std::string(fn) + "() expects argument " + std::to_string(i + 1) + " to be a boolean, got " + valueTypeName(args[i].type()),
                                loc);
            return args[i].asBool();
        }

        // A number the host gave back, which must be in the range the function promises.
        [[noreturn]] CUFF_COLD CUFF_NOINLINE inline void outOfRange(const char *fn, const char *what, double v, const char *range, const SourceLocation &loc)
        {
            failed(fn, std::string("the host gave ") + what + " " + formatCuffNumber(v) + ", which is not from " + range, loc);
        }

        inline double checked(const char *fn, const char *what, double v, double hi, const char *range, const SourceLocation &loc)
        {
            if (!(v >= 0 && v <= hi))
                outOfRange(fn, what, v, range, loc);
            return v;
        }

        inline const char *const kParts[] = {"battery", "button", "led", "beep", "screen_text", "serial", "sensor", "pin", "pwm", "analog"};

        inline bool given(const Hardware &h, const std::string &part)
        {
            if (part == "battery")
                return bool(h.battery);
            if (part == "button")
                return bool(h.button);
            if (part == "led")
                return bool(h.led);
            if (part == "beep")
                return bool(h.beep);
            if (part == "screen_text")
                return bool(h.screenText);
            if (part == "serial")
                return h.serialRead || h.serialWrite;
            if (part == "sensor")
                return bool(h.sensor);
            if (part == "pin")
                return h.pinRead || h.pinWrite;
            if (part == "pwm")
                return bool(h.pwm);
            return bool(h.analog);
        }

        inline Value battery(const Hardware &h, std::vector<Value> &args, const SourceLocation &loc)
        {
            expectArgCount("hw_battery", args, 0, loc);
            if (!h.battery)
                unsupported("hw_battery", "battery", "", loc);
            const BatteryState state = host("hw_battery", "battery", h.battery, loc);
            auto out = std::make_shared<ValueMap>();
            out->set("level", Value::makeNumber(checked("hw_battery", "a level of", state.level, 100, "0 to 100", loc)));
            out->set("charging", Value::makeBool(state.charging));
            return Value::makeMap(std::move(out));
        }

        inline Value button(const Hardware &h, std::vector<Value> &args, const SourceLocation &loc)
        {
            expectArgCount("hw_button", args, 1, loc);
            const int id = idArg("hw_button", args, 0, "the button", loc);
            if (!h.button)
                unsupported("hw_button", "button", "", loc);
            return Value::makeBool(host("hw_button", "button", [&] { return h.button(id); }, loc));
        }

        inline Value led(const Hardware &h, std::vector<Value> &args, const SourceLocation &loc)
        {
            expectArgCount("hw_led", args, 2, loc);
            const int id = idArg("hw_led", args, 0, "the light", loc);
            const bool on = boolArg("hw_led", args, 1, loc);
            if (!h.led)
                unsupported("hw_led", "led", "", loc);
            host("hw_led", "led", [&] { h.led(id, on); }, loc);
            return Value();
        }

        inline Value beep(const Hardware &h, std::vector<Value> &args, const SourceLocation &loc)
        {
            expectArgRange("hw_beep", args, 0, 2, loc);
            const double hertz = args.size() > 0 ? rangeArg("hw_beep", args, 0, "the frequency in hertz", 20, 20000, loc) : 880;
            const double seconds = args.size() > 1 ? rangeArg("hw_beep", args, 1, "the length in seconds", 0, 10, loc) : 0.2;
            if (!h.beep)
                unsupported("hw_beep", "beep", "", loc);
            host("hw_beep", "beep", [&] { h.beep(hertz, seconds); }, loc);
            return Value();
        }

        inline Value screenText(const Hardware &h, std::vector<Value> &args, const SourceLocation &loc)
        {
            expectArgCount("hw_screen_text", args, 2, loc);
            const int line = idArg("hw_screen_text", args, 0, "the line", loc);
            const std::string &text = expectStr("hw_screen_text", args, 1, loc);
            if (!h.screenText)
                unsupported("hw_screen_text", "screen_text", "", loc);
            host("hw_screen_text", "screen_text", [&] { h.screenText(line, text); }, loc);
            return Value();
        }

        // No argument reads what has arrived; a str sends it.
        inline Value serial(const Hardware &h, std::vector<Value> &args, const SourceLocation &loc)
        {
            expectArgRange("hw_serial", args, 0, 1, loc);
            if (args.empty())
            {
                if (!h.serialRead)
                    unsupported("hw_serial", "serial", h.serialWrite ? "this device's serial port can only send" : "", loc);
                std::string text = host("hw_serial", "serial", h.serialRead, loc);
                ensureStringSize(text.size(), loc);
                return Value::makeStr(std::move(text));
            }
            const std::string &text = expectStr("hw_serial", args, 0, loc);
            if (!h.serialWrite)
                unsupported("hw_serial", "serial", h.serialRead ? "this device's serial port can only receive" : "", loc);
            host("hw_serial", "serial", [&] { h.serialWrite(text); }, loc);
            return Value();
        }

        inline Value sensor(const Hardware &h, std::vector<Value> &args, const SourceLocation &loc)
        {
            expectArgCount("hw_sensor", args, 1, loc);
            const std::string &name = expectStr("hw_sensor", args, 0, loc);
            if (!h.sensor)
                unsupported("hw_sensor", "sensor", "", loc);
            const double v = host("hw_sensor", "sensor", [&] { return h.sensor(name); }, loc);
            if (!std::isfinite(v))
                failed("hw_sensor", "the host gave no number for \"" + name + "\"", loc);
            return Value::makeNumber(v);
        }

        // One argument reads the pin; a boolean after it sets the pin high (true) or low.
        inline Value pin(const Hardware &h, std::vector<Value> &args, const SourceLocation &loc)
        {
            expectArgRange("hw_pin", args, 1, 2, loc);
            const int id = idArg("hw_pin", args, 0, "the pin", loc);
            if (args.size() == 1)
            {
                if (!h.pinRead)
                    unsupported("hw_pin", "pin", h.pinWrite ? "this device's pins can only be set" : "", loc);
                return Value::makeBool(host("hw_pin", "pin", [&] { return h.pinRead(id); }, loc));
            }
            const bool high = boolArg("hw_pin", args, 1, loc);
            if (!h.pinWrite)
                unsupported("hw_pin", "pin", h.pinRead ? "this device's pins can only be read" : "", loc);
            host("hw_pin", "pin", [&] { h.pinWrite(id, high); }, loc);
            return Value();
        }

        inline Value pwm(const Hardware &h, std::vector<Value> &args, const SourceLocation &loc)
        {
            expectArgCount("hw_pwm", args, 2, loc);
            const int id = idArg("hw_pwm", args, 0, "the pin", loc);
            const double duty = rangeArg("hw_pwm", args, 1, "the duty", 0, 1, loc);
            if (!h.pwm)
                unsupported("hw_pwm", "pwm", "", loc);
            host("hw_pwm", "pwm", [&] { h.pwm(id, duty); }, loc);
            return Value();
        }

        inline Value analog(const Hardware &h, std::vector<Value> &args, const SourceLocation &loc)
        {
            expectArgCount("hw_analog", args, 1, loc);
            const int id = idArg("hw_analog", args, 0, "the pin", loc);
            if (!h.analog)
                unsupported("hw_analog", "analog", "", loc);
            return Value::makeNumber(checked("hw_analog", "a reading of", host("hw_analog", "analog", [&] { return h.analog(id); }, loc), 1, "0 to 1", loc));
        }

        inline Value supports(const Hardware &h, std::vector<Value> &args, const SourceLocation &loc)
        {
            expectArgCount("hw_supports", args, 1, loc);
            const std::string &part = expectStr("hw_supports", args, 0, loc);
            if (std::find_if(std::begin(kParts), std::end(kParts), [&](const char *p) { return part == p; }) == std::end(kParts))
                throw ValueError("hw_supports() expects one of \"battery\", \"button\", \"led\", \"beep\", \"screen_text\", \"serial\", \"sensor\", "
                                 "\"pin\", \"pwm\", \"analog\", got \"" + part + "\"",
                                 loc);
            if (!given(h, part))
                return Value::makeBool(false);
            if (!h.supports)
                return Value::makeBool(true);
            return Value::makeBool(host("hw_supports", part.c_str(), [&] { return h.supports(part); }, loc));
        }

    }

    inline void registerHardwareDLC(std::unordered_map<std::string, NativeFn> &reg, const HardwareDLCOptions &opts)
    {
        static const Hardware none;
        static const struct
        {
            const char *name;
            Value (*fn)(const Hardware &, std::vector<Value> &, const SourceLocation &);
        } functions[] = {{"hw_battery", &hw::battery}, {"hw_button", &hw::button},   {"hw_led", &hw::led},         {"hw_beep", &hw::beep},
                         {"hw_screen_text", &hw::screenText}, {"hw_serial", &hw::serial}, {"hw_sensor", &hw::sensor}, {"hw_pin", &hw::pin},
                         {"hw_pwm", &hw::pwm},        {"hw_analog", &hw::analog},   {"hw_supports", &hw::supports}};
        const Hardware *parts = opts.hardware ? opts.hardware : &none;
        for (const auto &f : functions)
            reg[f.name] = hw::Call{parts, f.fn};
    }

}
