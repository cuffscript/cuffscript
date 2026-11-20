#pragma once

// What `cuffc --hardware` and `cuffsh --hardware` give DLC:hardware: the parts of a desktop or laptop computer that can be
// reached without a library. It is not part of the engine's run, only one host's choice: a host that embeds the engine
// gives the parts of its own device (Engine::Options::hardware), and may start from this one.
//
//   battery       Linux: /sys/class/power_supply. Windows: GetSystemPowerStatus.
//   temperature   hw_sensor("temperature"), in degrees Celsius. Linux: the first thermal zone in /sys/class/thermal.
//   beep          Windows: the speaker, at the frequency and for the time asked (the call returns when it ends).
//                 Elsewhere: the bell of the terminal (/dev/tty), whose sound and length are the terminal's.
//
// A computer has no buttons, lights, pins or serial line that a program should drive without being told which, so those
// are not given; nor is a "screen" beside the terminal that `print` already writes to. On macOS only the beep is given.

#include "CuffEngine.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace cuff
{

    namespace desktop
    {

        // The first line of a small file, without the line break; nothing when it cannot be read.
        CUFF_NOINLINE inline std::string firstLine(const std::filesystem::path &file)
        {
            std::ifstream in(file);
            std::string line;
            if (!in || !std::getline(in, line))
                return std::string();
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                line.pop_back();
            return line;
        }

        inline bool wholeNumber(const std::string &s, long &out)
        {
            if (s.empty() || s.size() > 12)
                return false;
            size_t i = s[0] == '-' ? 1 : 0;
            if (i == s.size())
                return false;
            long v = 0;
            for (; i < s.size(); ++i)
            {
                if (s[i] < '0' || s[i] > '9')
                    return false;
                v = v * 10 + (s[i] - '0');
            }
            out = s[0] == '-' ? -v : v;
            return true;
        }

        // The names in a folder, sorted, so that "the first" is the same every time; none when it cannot be read.
        CUFF_NOINLINE inline std::vector<std::string> entries(const std::filesystem::path &dir)
        {
            std::vector<std::string> out;
            std::error_code ec;
            for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
                out.push_back(it->path().filename().string());
            std::sort(out.begin(), out.end());
            return out;
        }

        // Linux: a power supply of type Battery that tells its capacity.
        inline bool sysBattery(const std::filesystem::path &sys, BatteryState &state)
        {
            const std::filesystem::path dir = sys / "class" / "power_supply";
            for (const std::string &name : entries(dir))
            {
                const std::filesystem::path supply = dir / name;
                if (firstLine(supply / "type") != "Battery")
                    continue;
                long capacity = 0;
                if (!wholeNumber(firstLine(supply / "capacity"), capacity) || capacity < 0)
                    continue;
                state.level = static_cast<double>(std::min(capacity, 100L));
                state.charging = firstLine(supply / "status") == "Charging";
                return true;
            }
            return false;
        }

        // Linux: the first thermal zone that gives a reading, in thousandths of a degree.
        inline bool sysTemperature(const std::filesystem::path &sys, double &celsius)
        {
            const std::filesystem::path dir = sys / "class" / "thermal";
            for (const std::string &name : entries(dir))
            {
                if (name.rfind("thermal_zone", 0) != 0)
                    continue;
                long milli = 0;
                if (wholeNumber(firstLine(dir / name / "temp"), milli))
                {
                    celsius = static_cast<double>(milli) / 1000.0;
                    return true;
                }
            }
            return false;
        }

#if !defined(_WIN32)
        inline bool canRing()
        {
            const int fd = ::open("/dev/tty", O_WRONLY | O_NOCTTY);
            if (fd < 0)
                return false;
            ::close(fd);
            return true;
        }
#endif

#if defined(_WIN32)
        inline bool windowsBattery(BatteryState &state)
        {
            SYSTEM_POWER_STATUS status;
            if (!GetSystemPowerStatus(&status) || (status.BatteryFlag & 128) || status.BatteryFlag == 255 || status.BatteryLifePercent > 100)
                return false;
            state.level = status.BatteryLifePercent;
            state.charging = (status.BatteryFlag & 8) != 0;
            return true;
        }
#endif

    }

    // `sysRoot` is where Linux keeps /sys; a test gives a folder of its own.
    inline Hardware desktopHardware(const std::string &sysRoot = "/sys")
    {
        Hardware parts;
#if defined(_WIN32)
        (void)sysRoot;
        parts.battery = []
        {
            BatteryState state;
            if (!desktop::windowsBattery(state))
                throw HardwareUnsupported("this computer has no battery");
            return state;
        };
        parts.beep = [](double hertz, double seconds)
        {
            // Beep() takes 37 Hz and up; it returns when the sound has ended, or at once on a computer without a speaker.
            ::Beep(static_cast<DWORD>(std::max(37.0, hertz)), static_cast<DWORD>(seconds * 1000.0 + 0.5));
        };
        parts.supports = [](const std::string &part)
        {
            BatteryState state;
            return part != "battery" || desktop::windowsBattery(state);
        };
#else
#if defined(__linux__)
        const std::filesystem::path sys(sysRoot);
        parts.battery = [sys]
        {
            BatteryState state;
            if (!desktop::sysBattery(sys, state))
                throw HardwareUnsupported("this computer has no battery");
            return state;
        };
        parts.sensor = [sys](const std::string &name)
        {
            double celsius = 0;
            if (name != "temperature")
                throw HardwareUnsupported("this computer has no sensor \"" + name + "\"; it may have \"temperature\"");
            if (!desktop::sysTemperature(sys, celsius))
                throw HardwareUnsupported("this computer tells no temperature");
            return celsius;
        };
        parts.supports = [sys](const std::string &part)
        {
            BatteryState state;
            double celsius = 0;
            if (part == "battery")
                return desktop::sysBattery(sys, state);
            if (part == "sensor")
                return desktop::sysTemperature(sys, celsius);
            return part != "beep" || desktop::canRing();
        };
#else
        (void)sysRoot;
        parts.supports = [](const std::string &part) { return part != "beep" || desktop::canRing(); };
#endif
        parts.beep = [](double, double)
        {
            const int fd = ::open("/dev/tty", O_WRONLY | O_NOCTTY);
            if (fd < 0)
                throw HardwareUnsupported("there is no terminal to ring");
            const char bell = '\a';
            const ssize_t written = ::write(fd, &bell, 1);
            ::close(fd);
            if (written != 1)
                throw HardwareUnsupported("the terminal could not be rung");
        };
#endif
        return parts;
    }

}
