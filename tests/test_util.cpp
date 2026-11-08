#include <climits>

#include <gtest/gtest.h>

#include "tgui/util.h"

using tgui::formatEta;
using tgui::formatRatio;
using tgui::formatRate;
using tgui::formatSeconds;
using tgui::formatSize;
using tgui::percentComplete;

TEST(UtilFormatSize, TransguiParity)
{
    // GetHumanSize(0, 0) → "0 b"
    EXPECT_EQ(formatSize(0), "0 b");
    EXPECT_EQ(formatSize(500), "500 b");
    // 1024 b → "1 KB"
    EXPECT_EQ(formatSize(1024), "1 KB");
    // 1.5 MiB → "2 MB" (0 знаков, как в transgui)
    EXPECT_EQ(formatSize(1536 * 1024), "2 MB");
    // GB → 2 знака: 1.23 GB
    EXPECT_EQ(formatSize(1.23 * 1024 * 1024 * 1024), "1.23 GB");
    // TB → 3 знака: 1.234 TB
    EXPECT_EQ(formatSize(1.234 * 1024.0 * 1024 * 1024 * 1024), "1.234 TB");
    // Отрицательное → "-" (EmptyStr)
    EXPECT_EQ(formatSize(-1), "-");
}

TEST(UtilFormatSize, RateMode)
{
    // roundTo = 1: минимальная единица KB, 1 знак.
    EXPECT_EQ(formatSize(123456, 1), "120.6 KB");
    EXPECT_EQ(formatSize(500, 1), "0 KB");
}

TEST(UtilFormatRate, PerSecond)
{
    EXPECT_EQ(formatRate(123456), "120.6 KB/s");
    EXPECT_EQ(formatRate(0), "0 KB/s");
}

TEST(UtilFormatSeconds, TransguiParity)
{
    EXPECT_EQ(formatSeconds(0), "0s");
    EXPECT_EQ(formatSeconds(45), "45s");
    EXPECT_EQ(formatSeconds(60), "1m");
    EXPECT_EQ(formatSeconds(330), "5m, 30s");
    EXPECT_EQ(formatSeconds(3600), "1h");
    EXPECT_EQ(formatSeconds(5400), "1h, 30m");
    EXPECT_EQ(formatSeconds(86400), "1d");
    EXPECT_EQ(formatSeconds(205200), "2d, 6h");
}

TEST(UtilFormatEta, TransguiParity)
{
    // ∞ → ""
    EXPECT_EQ(formatEta(-1), "");
    EXPECT_EQ(formatEta(INT32_MAX), "");
    // Без округления
    EXPECT_EQ(formatEta(45), "45s");
    EXPECT_EQ(formatEta(120), "2m");
    // > 2 мин → округление до минуты
    EXPECT_EQ(formatEta(150), "2m, 30s");
    EXPECT_EQ(formatEta(180), "3m");
    // > 30 мин → округление до 5 минут
    EXPECT_EQ(formatEta(2000), "35m");
    // > 2 ч → округление до часа
    EXPECT_EQ(formatEta(7500), "2h");
    EXPECT_EQ(formatEta(10000), "3h");
}

TEST(UtilFormatRatio, TransguiParity)
{
    EXPECT_EQ(formatRatio(-1), "");
    EXPECT_EQ(formatRatio(-2), "\u221E"); // ∞
    EXPECT_EQ(formatRatio(static_cast<double>(INT32_MAX)), "\u221E");
    EXPECT_EQ(formatRatio(1.5), "1.500");
    EXPECT_EQ(formatRatio(0), "0.000");
}

TEST(UtilPercentComplete, TransguiParity)
{
    EXPECT_DOUBLE_EQ(percentComplete(1000, 250), 75.0);
    EXPECT_DOUBLE_EQ(percentComplete(0, 0), 0.0);
    // left > size (не должно быть, но transgui жмёт на 0)
    EXPECT_DOUBLE_EQ(percentComplete(100, 500), 0.0);
}