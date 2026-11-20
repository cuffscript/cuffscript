// DLC:hardware: a host that gives a device's parts, one that gives none, what reaches the host and what comes back,
// and the parts cuffc and cuffsh give with --hardware, read from a /sys of the test's own.
#include "engine/CuffEngine.h"
#include "engine/DesktopHardware.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using cuff::Engine;

    int pass = 0, fail = 0;

    void check(bool cond, const std::string &msg)
    {
        if (cond)
            ++pass;
        else
        {
            ++fail;
            std::cout << "FAIL: " << msg << "\n";
        }
    }

    bool has(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }

    struct Ran
    {
        bool ok;
        std::string out, error;
    };

    Ran run(Engine &engine, const std::string &source)
    {
        std::ostringstream out;
        std::streambuf *old = std::cout.rdbuf(out.rdbuf());
        Engine::Outcome r = engine.run("use DLC:hardware\n" + source);
        std::cout.rdbuf(old);
        return {r.success, out.str(), r.error};
    }

    // A device with every part, which writes down what the program did to it.
    struct Device
    {
        std::vector<std::string> log;
        bool pressed = true;
        std::string inbox = "hello";
        double level = 76;

        cuff::Hardware parts()
        {
            cuff::Hardware h;
            h.battery = [this] { return cuff::BatteryState{level, true}; };
            h.button = [this](int id) { return id == 2 && pressed; };
            h.led = [this](int id, bool on) { log.push_back("led " + std::to_string(id) + (on ? " on" : " off")); };
            h.beep = [this](double hz, double s) { log.push_back("beep " + std::to_string(int(hz)) + " " + std::to_string(int(s * 1000))); };
            h.screenText = [this](int line, const std::string &text) { log.push_back("screen " + std::to_string(line) + " " + text); };
            h.serialWrite = [this](const std::string &text) { log.push_back("send " + text); };
            h.serialRead = [this] { return std::exchange(inbox, std::string()); };
            h.sensor = [](const std::string &name) -> double
            {
                if (name == "temperature")
                    return 21.5;
                if (name == "broken")
                    throw std::runtime_error("the sensor did not answer");
                if (name == "nan")
                    return std::nan("");
                throw cuff::HardwareUnsupported("there is no sensor \"" + name + "\"");
            };
            h.pinRead = [](int pin) { return pin % 2 == 1; };
            h.pinWrite = [this](int pin, bool high) { log.push_back("pin " + std::to_string(pin) + (high ? " high" : " low")); };
            h.pwm = [this](int pin, double duty) { log.push_back("pwm " + std::to_string(pin) + " " + std::to_string(int(duty * 100))); };
            h.analog = [](int pin) { return pin == 9 ? 1.5 : 0.25; };
            return h;
        }
    };

    std::string joined(const std::vector<std::string> &log)
    {
        std::string out;
        for (const std::string &line : log)
            out += line + "\n";
        return out;
    }

    void givenTests()
    {
        Device device;
        Engine::Options options;
        options.hardware = device.parts();
        Engine engine(options);

        Ran r = run(engine, "map b = hw_battery()\nprint(b[\"level\"], b[\"charging\"])\n"
                            "print(hw_button(2), hw_button(1))\n"
                            "hw_led(0, true)\nhw_led(65535, false)\n"
                            "hw_beep()\nhw_beep(440)\nhw_beep(20000, 10)\n"
                            "hw_screen_text(1, \"21.5 C\")\n"
                            "hw_serial(\"ping\")\nprint(hw_serial(), length(hw_serial()))\n"
                            "print(hw_sensor(\"temperature\"))\n"
                            "print(hw_pin(3), hw_pin(4))\nhw_pin(5, true)\nhw_pin(5, false)\n"
                            "hw_pwm(6, 0.5)\nhw_pwm(6, 0)\nhw_pwm(6, 1)\n"
                            "print(hw_analog(2))\n");
        check(r.ok, "every part, given: " + r.error);
        check(r.out == "76 true\ntrue false\nhello 0\n21.5\ntrue false\n0.25\n", "what the program read: " + r.out);
        check(joined(device.log) == "led 0 on\nled 65535 off\nbeep 880 200\nbeep 440 200\nbeep 20000 10000\nscreen 1 21.5 C\nsend ping\n"
                                    "pin 5 high\npin 5 low\npwm 6 50\npwm 6 0\npwm 6 100\n",
              "what reached the device:\n" + joined(device.log));

        r = run(engine, "list parts = [\"battery\", \"button\", \"led\", \"beep\", \"screen_text\", \"serial\", \"sensor\", \"pin\", \"pwm\", \"analog\"]\n"
                        "loop each str p in parts do:\n    if !hw_supports(p) do:\n        print(p)\n    end\nend\nprint(\"all\")\n");
        check(r.ok && r.out == "all\n", "hw_supports is true for every part given: " + r.out + r.error);

        // What the host throws, and what it gives back that it should not.
        auto code = [&](const std::string &line) -> std::string
        {
            Engine fresh(options);
            const Ran x = run(fresh, line + "\n");
            const size_t at = x.error.find("[E");
            return x.ok ? std::string("ok") : x.error.substr(at, 8) + " " + x.error.substr(0, std::string::npos);
        };
        std::string c = code("hw_sensor(\"humidity\")");
        check(has(c, "[E4-044]") && has(c, "hw_sensor(): there is no sensor \"humidity\""), "HardwareUnsupported from the host is E4-044 with its text: " + c);
        c = code("hw_sensor(\"broken\")");
        check(has(c, "[E4-042]") && has(c, "hw_sensor() failed: the sensor did not answer"), "another exception from the host is E4-042: " + c);
        c = code("hw_sensor(\"nan\")");
        check(has(c, "[E4-042]"), "a sensor that gives no number is a failure of the host: " + c);
        c = code("hw_analog(9)");
        check(has(c, "[E4-042]") && has(c, "1.5"), "an analog reading above 1 is a failure of the host: " + c);
        device.level = 120;
        c = code("hw_battery()");
        check(has(c, "[E4-042]") && has(c, "120"), "a battery level above 100 is a failure of the host: " + c);
        device.level = 50;

        // Arguments are checked before the host is asked.
        for (const char *line : {"hw_led(-1, true)", "hw_led(1.5, true)", "hw_led(65536, true)", "hw_pwm(1, 1.01)", "hw_pwm(1, -0.1)",
                                 "hw_beep(19)", "hw_beep(20001)", "hw_beep(440, 10.5)", "hw_beep(440, -1)", "hw_supports(\"radio\")",
                                 "hw_screen_text(-1, \"x\")"})
            check(has(code(line), "[E4-025]"), std::string("a value out of range: ") + line + " " + code(line));
        for (const char *line : {"hw_led(1, 1)", "hw_pin(1, \"high\")", "hw_serial(5)", "hw_sensor(1)", "hw_button(\"a\")"})
            check(has(code(line), "[E4-005]"), std::string("a value of the wrong type: ") + line + " " + code(line));
        for (const char *line : {"hw_battery(1)", "hw_pin()", "hw_pin(1, true, 2)", "hw_serial(\"a\", \"b\")", "hw_beep(440, 1, 1)"})
            check(has(code(line), "[E4-010]"), std::string("a wrong number of arguments: ") + line + " " + code(line));
        check(device.log.size() == 12, "and none of those reached the device");

        // A host that knows only when it looks.
        Engine::Options refined = options;
        refined.hardware.supports = [](const std::string &part) { return part != "battery"; };
        Engine asked(refined);
        r = run(asked, "print(hw_supports(\"battery\"), hw_supports(\"led\"))\n");
        check(r.ok && r.out == "false true\n", "the host's supports() decides for a part it gives: " + r.out + r.error);
    }

    void notGivenTests()
    {
        Engine engine;
        Ran r = run(engine, "print(hw_supports(\"battery\"), hw_supports(\"pin\"))\n");
        check(r.ok && r.out == "false false\n", "a host that gives nothing: hw_supports is false: " + r.out + r.error);
        for (const char *line : {"hw_battery()", "hw_button(0)", "hw_led(0, true)", "hw_beep()", "hw_screen_text(0, \"x\")", "hw_serial()",
                                 "hw_serial(\"x\")", "hw_sensor(\"temperature\")", "hw_pin(0)", "hw_pin(0, true)", "hw_pwm(0, 0.5)", "hw_analog(0)"})
        {
            Engine fresh;
            r = run(fresh, std::string(line) + "\n");
            check(!r.ok && has(r.error, "[E4-044]") && has(r.error, "is not supported on this device"), std::string("not given: ") + line + " " + r.error);
        }
        r = run(engine, "hw_battery() or_else as Error e do:\n    print(e.code)\nend\nprint(\"went on\")\n");
        check(r.ok && r.out == "E4-044\nwent on\n", "E4-044 is caught by or_else: " + r.out + r.error);

        // A part given for one direction only.
        Engine::Options half;
        half.hardware.pinRead = [](int) { return true; };
        half.hardware.serialWrite = [](const std::string &) {};
        Engine e(half);
        r = run(e, "print(hw_supports(\"pin\"), hw_supports(\"serial\"), hw_pin(1))\nhw_pin(1, true)\n");
        check(!r.ok && r.out == "true true true\n" && has(r.error, "[E4-044]") && has(r.error, "can only be read"), "pins that can only be read: " + r.out + r.error);
        Engine e2(half);
        r = run(e2, "hw_serial()\n");
        check(!r.ok && has(r.error, "can only send"), "a serial line that can only send: " + r.error);

        Engine without;
        const Engine::Outcome o = without.run("hw_battery()\n");
        check(!o.success && has(o.error, "[E4-002]"), "without use DLC:hardware the functions are not there: " + o.error);
    }

    // The engine keeps the parts with its options: reset() keeps them, and an engine copied from the options has its own.
    void lifetimeTests()
    {
        Device device;
        Engine::Options options;
        options.hardware = device.parts();
        Engine engine(options);
        options.hardware = cuff::Hardware(); // the engine has its own copy
        Ran r = run(engine, "hw_led(1, true)\n");
        check(r.ok && device.log.size() == 1, "the engine keeps the parts it was made with: " + r.error);
        engine.reset();
        r = run(engine, "hw_led(2, false)\n");
        check(r.ok && device.log.size() == 2, "and keeps them across reset(): " + r.error);
    }

    // What cuffc --hardware gives, against a /sys of the test's own.
    void desktopTests()
    {
#if defined(__linux__)
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / ("cuff_hw_test_" + std::to_string(::getpid()));
        fs::remove_all(root);
        auto write = [&](const fs::path &file, const std::string &text)
        {
            fs::create_directories(file.parent_path());
            std::ofstream(file) << text;
        };

        Engine::Options options;
        options.hardware = cuff::desktopHardware(root.string());
        Engine none(options);
        Ran r = run(none, "print(hw_supports(\"battery\"), hw_supports(\"sensor\"), hw_supports(\"led\"))\n");
        check(r.ok && r.out == "false false false\n", "a computer without a battery or a thermal zone: " + r.out + r.error);
        r = run(none, "hw_battery()\n");
        check(!r.ok && has(r.error, "[E4-044]") && has(r.error, "this computer has no battery"), "no battery is E4-044: " + r.error);

        write(root / "class/power_supply/AC/type", "Mains\n");
        write(root / "class/power_supply/AC/online", "1\n");
        write(root / "class/power_supply/BAT0/type", "Battery\n");
        write(root / "class/power_supply/BAT0/capacity", "64\n");
        write(root / "class/power_supply/BAT0/status", "Charging\n");
        write(root / "class/thermal/cooling_device0/type", "Processor\n");
        write(root / "class/thermal/thermal_zone0/temp", "not a number\n");
        write(root / "class/thermal/thermal_zone1/temp", "47250\n");
        Engine some(options);
        r = run(some, "map b = hw_battery()\nprint(b[\"level\"], b[\"charging\"], hw_sensor(\"temperature\"))\n"
                      "print(hw_supports(\"battery\"), hw_supports(\"sensor\"))\n");
        check(r.ok && r.out == "64 true 47.25\ntrue true\n", "the battery and the first thermal zone with a reading: " + r.out + r.error);
        r = run(some, "hw_sensor(\"humidity\")\n");
        check(!r.ok && has(r.error, "[E4-044]") && has(r.error, "no sensor \"humidity\""), "a sensor a computer does not have: " + r.error);

        write(root / "class/power_supply/BAT0/status", "Discharging\n");
        write(root / "class/power_supply/BAT0/capacity", "130\n");
        r = run(some, "map again = hw_battery()\nprint(again[\"level\"], again[\"charging\"])\n");
        check(r.ok && r.out == "100 false\n", "a capacity above 100 is read as full, and Discharging is not charging: " + r.out + r.error);
        fs::remove_all(root);
#endif
    }
}

int main()
{
    givenTests();
    notGivenTests();
    lifetimeTests();
    desktopTests();
    std::cout << pass << " passed, " << fail << " failed\n";
    return fail == 0 ? 0 : 1;
}
