#include "tgui/util.h"

#include <climits>
#include <cstdio>

namespace tgui {
namespace {

std::string formatFixed(double value, int decimals, const char* unit)
{
    char fmt[32];
    std::snprintf(fmt, sizeof(fmt), "%%%df %s", decimals, unit);
    char buf[96];
    std::snprintf(buf, sizeof(buf), fmt, value);
    return buf;
}

} // namespace

std::string formatSize(double bytes, int roundTo)
{
    if (bytes < 0)
        return "-";

    static const char* units[] = {"b", "KB", "MB", "GB", "TB"};
    int unit = 0; // 'b'
    if (roundTo > 0) {
        unit = 1; // 'KB'
        bytes /= 1024.0;
    }
    while (unit < 4 && bytes >= 1024.0) {
        bytes /= 1024.0;
        ++unit;
    }
    // transgui: if (RoundTo = 0) and (i > 3) then RoundTo := i - 2;
    // (1-based i: 4 = GB → 2 знака, 5 = TB → 3 знака).
    if (roundTo == 0 && unit > 2)
        roundTo = unit - 1;
    return formatFixed(bytes, roundTo, units[static_cast<size_t>(unit)]);
}

std::string formatRate(double bytesPerSec)
{
    return formatSize(bytesPerSec, 1) + "/s";
}

std::string formatSeconds(int64_t j)
{
    if (j < 0)
        return "";
    if (j < 60)
        return std::to_string(j) + "s";
    if (j < 3600) {
        std::string r = std::to_string(j / 60) + "m";
        j %= 60;
        if (j > 0)
            r += ", " + std::to_string(j) + "s";
        return r;
    }
    j = (j + 30) / 60; // округление минут
    if (j < 1440) {
        std::string r = std::to_string(j / 60) + "h";
        j %= 60;
        if (j > 0)
            r += ", " + std::to_string(j) + "m";
        return r;
    }
    j = (j + 30) / 60; // округление часов
    std::string r = std::to_string(j / 24) + "d";
    j %= 24;
    if (j > 0)
        r += ", " + std::to_string(j) + "h";
    return r;
}

std::string formatEta(int64_t seconds)
{
    if (seconds < 0 || seconds == INT32_MAX)
        return "";

    int64_t eta = seconds;
    if (eta > 2 * 3600) {
        eta = (eta + 1800) / 3600 * 3600; // до часа
    } else if (eta > 30 * 60) {
        eta = (eta + 150) / 300 * 300; // до 5 минут
    } else if (eta > 2 * 60) {
        eta = (eta + 30) / 60 * 60; // до минуты
    }
    return formatSeconds(eta);
}

std::string formatRatio(double ratio)
{
    if (ratio == static_cast<double>(INT32_MAX) || ratio == -2.0)
        return "\u221E"; // ∞
    if (ratio == -1.0)
        return "";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", ratio);
    return buf;
}

double percentComplete(double sizeWhenDone, double leftUntilDone)
{
    if (sizeWhenDone <= 0.0)
        return 0.0;
    double p = (sizeWhenDone - leftUntilDone) * 100.0 / sizeWhenDone;
    if (p < 0.0)
        p = 0.0;
    return p;
}

} // namespace tgui
