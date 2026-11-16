#pragma once

#include <cstdint>
#include <string>

namespace tgui {

// Форматирование числовых значений для UI: размеры, скорости, время,
// ratio. Единицы — b/KB/MB/GB/TB (не переводятся).

// GetHumanSize(bytes, roundTo): "0 b", "500 b", "1 KB", "1536 MB" (roundTo=0),
// "1.23 GB", "1.234 TB". roundTo=1 — минимальная единица KB (скорости).
// Отрицательное значение → "-" (EmptyStr).
std::string formatSize(double bytes, int roundTo = 0);

// Скорость: GetHumanSize(x, 1) + "/s" — "120.6 KB/s".
std::string formatRate(double bytesPerSec);

// SecondsToString: "45s", "5m, 30s", "1h, 20m", "2d, 3h".
std::string formatSeconds(int64_t seconds);

// EtaToString: "∞" (отрицательное или INT32_MAX) → "";
// > 2 ч — округление до часа, > 30 мин — до 5 мин, > 2 мин — до минуты.
std::string formatEta(int64_t seconds);

// RatioToString: -1 → ""; -2 или INT32_MAX → "∞"; иначе "1.500".
std::string formatRatio(double ratio);

// Прогресс торрента 0..100 ((sizeWhenDone - leftUntilDone) / sizeWhenDone).
double percentComplete(double sizeWhenDone, double leftUntilDone);

} // namespace tgui
