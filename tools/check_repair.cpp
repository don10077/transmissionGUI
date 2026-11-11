// Одноразовая проверка: ремонт реального повреждённого .torrent.
#include "tgui/bencode.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <corrupt.torrent> [expected.torrent]\n", argv[0]);
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    std::string raw((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());

    tgui::bencode::Value v;
    std::string err;
    const bool strict = tgui::bencode::decode(raw, v, &err);
    std::printf("strict decode: %s%s%s\n", strict ? "OK" : "FAIL",
                strict ? "" : " (", strict ? "" : err.c_str());
    if (!strict) std::printf(")\n");

    tgui::bencode::RepairResult rep;
    std::string rerr;
    if (!tgui::bencode::repair(raw, rep, &rerr)) {
        std::printf("repair: FAIL (%s)\n", rerr.c_str());
        return 1;
    }
    std::printf("repair: OK, %zu fixes\n", rep.fixes.size());
    for (const auto& f : rep.fixes)
        std::printf("  pos=%zu  %s  (%s)\n", f.pos, f.description.c_str(),
                    f.oldLength == "missing-scheme" ? "scheme" : "length");
    std::printf("touches info: %s\n",
                tgui::bencode::repairTouchesInfo(rep.fixedBytes, rep.fixes)
                    ? "YES" : "no");

    // Валидность результата
    tgui::bencode::Value r;
    std::string rerr2;
    if (!tgui::bencode::decode(rep.fixedBytes, r, &rerr2)) {
        std::printf("result decode: FAIL (%s)\n", rerr2.c_str());
        return 1;
    }
    std::printf("result decode: OK\n");
    // Проверяем фактические байты, а не флаг алгоритма; URL с passkey не выводим.
    for (const char* key : {"info", "piece layers"}) {
        if (const auto* value = r.find(key)) {
            const std::string encoded = tgui::bencode::encode(*value);
            if (raw.find(encoded) == std::string::npos ||
                rep.fixedBytes.find(encoded) == std::string::npos) {
                std::printf("%s: bytes changed\n", key);
                return 1;
            }
            std::printf("%s: bytes preserved\n", key);
        }
    }

    if (argc >= 3) {
        std::ifstream ex(argv[2], std::ios::binary);
        std::string expected((std::istreambuf_iterator<char>(ex)),
                             std::istreambuf_iterator<char>());
        if (rep.fixedBytes == expected)
            std::printf("IDENTICAL to expected file\n");
        else
            std::printf("DIFFERS from expected file (len %zu vs %zu)\n",
                        rep.fixedBytes.size(), expected.size());
    }
    return 0;
}