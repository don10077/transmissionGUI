#include <gtest/gtest.h>

#include "tgui/bencode.h"

using tgui::bencode::Value;
using tgui::bencode::decode;
using tgui::bencode::encode;
using tgui::bencode::repair;
using tgui::bencode::repairTouchesInfo;
using tgui::bencode::RepairResult;

TEST(Bencode, EncodePrimitives)
{
    EXPECT_EQ(encode(Value::integer(0)), "i0e");
    EXPECT_EQ(encode(Value::integer(-42)), "i-42e");
    EXPECT_EQ(encode(Value::string("hello")), "5:hello");
    EXPECT_EQ(encode(Value::string("")), "0:");
}

TEST(Bencode, EncodeListAndDict)
{
    // l i0e i1e i2e e
    EXPECT_EQ(encode(Value::list({Value::integer(0), Value::integer(1), Value::integer(2)})),
              "li0ei1ei2ee");

    // d3:bar4:spam3:baz11:another key4:spami42ee  (ключи отсортированы)
    const std::string expected = "d3:bar4:spam3:baz11:another key4:spami42ee";
    EXPECT_EQ(encode(Value::dictionary({{"bar", Value::string("spam")},
                                        {"baz", Value::string("another key")},
                                        {"spam", Value::integer(42)}})),
              expected);
}

TEST(Bencode, DecodeRoundTrip)
{
    const Value original = Value::dictionary({
        {"announce", Value::string("http://tracker/announce")},
        {"info", Value::dictionary({{"name", Value::string("test")},
                                    {"piece length", Value::integer(16384)},
                                    {"pieces", Value::string("x")},
                                    {"private", Value::integer(1)}})},
        {"nodes", Value::list({Value::string("a"), Value::string("b")})},
    });
    const std::string encoded = encode(original);
    Value decoded;
    std::string error;
    ASSERT_TRUE(decode(encoded, decoded, &error)) << error;
    EXPECT_EQ(encode(decoded), encoded);
    EXPECT_EQ(decoded.find("info")->find("piece length")->toInt(), 16384);
}

TEST(Bencode, DecodeStringEscapesBinary)
{
    // Строка с двоиковыми байтами (как pieces-хэши в .torrent).
    const std::string payload = std::string("\x00\x01\x02\x03\x04", 5);
    const Value v = Value::string(payload);
    const std::string encoded = encode(v);
    Value decoded;
    ASSERT_TRUE(decode(encoded, decoded));
    EXPECT_EQ(decoded.toString(), payload);
}

TEST(Bencode, DecodeErrors)
{
    Value out;
    std::string error;
    EXPECT_FALSE(decode("", out, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(decode("i42", out, &error));        // нет 'e'
    EXPECT_FALSE(decode("5:abc", out, &error));      // 5 байт, а дано 3
    EXPECT_FALSE(decode("li1eeX", out, &error));     // хвост после значения
    EXPECT_FALSE(decode("dbaz3:spame", out, &error)); // одиночный ключ без пары

    // Нерассортированные ключи — ошибка (bittorrent spec).
    EXPECT_FALSE(decode("d1:b1:a2:ffee", out, &error)); // b, a, f
    // Дублирующийся ключ — ошибка.
    EXPECT_FALSE(decode("d1:a1:a2:ffee", out, &error));
}

// --- repair() ---

namespace {

std::string makeSampleTorrent()
{
    const Value root = Value::dictionary({
        {"announce", Value::string("http://tracker.example.com/announce")},
        {"announce-list",
         Value::list({Value::list(
             {Value::string("http://tracker.example.com/announce"),
              Value::string("https://mirror.example.com/announce")})})},
        {"comment", Value::string("https://kinozal.example/details.php?id=12345")},
        {"info", Value::dictionary({{"name", Value::string("test.mkv")},
                                    {"piece length", Value::integer(16384)},
                                    {"pieces", Value::string(std::string(20, 'x'))},
                                    {"private", Value::integer(1)}})},
    });
    return encode(root);
}

// Помогает в тестах: заменить все вхождения old на new (ASSERT на число).
std::string replaceAll(std::string s, const std::string& oldSub,
                       const std::string& newSub, size_t expectedCount)
{
    size_t count = 0;
    size_t offset = 0;
    while (true) {
        const size_t pos = s.find(oldSub, offset);
        if (pos == std::string::npos)
            break;
        s.replace(pos, oldSub.size(), newSub);
        offset = pos + newSub.size();
        ++count;
    }
    EXPECT_EQ(count, expectedCount);
    return s;
}

} // namespace

TEST(Bencode, RepairValidFileNoop)
{
    const std::string ok = makeSampleTorrent();
    RepairResult rep;
    std::string error;
    ASSERT_TRUE(repair(ok, rep, &error)) << error;
    EXPECT_TRUE(rep.fixes.empty());
    EXPECT_EQ(rep.fixedBytes, ok); // побайтово неизменён
}

TEST(Bencode, RepairKinozalPattern)
{
    // Типовая порча «обработчиков ссылок»:
    //  (A) вырезан scheme «http:» в announce/announce-list (длина не пересчитана);
    //  (B) домен в comment подменён на более длинный (длина не пересчитана).
    std::string s = makeSampleTorrent();
    s = replaceAll(s, "http://tracker.example.com", "//tracker.example.com", 2);
    s = replaceAll(s, "kinozal.example", "kinozalnew.example", 1);

    Value out;
    std::string error;
    ASSERT_FALSE(decode(s, out, &error)); // строго — уже не декодируется

    RepairResult rep;
    ASSERT_TRUE(repair(s, rep, &error)) << error;
    EXPECT_EQ(rep.fixes.size(), 3u);

    Value decoded;
    ASSERT_TRUE(decode(rep.fixedBytes, decoded, &error)) << error;
    EXPECT_EQ(decoded.find("announce")->toString(),
              "http://tracker.example.com/announce"); // scheme восстановлена
    const auto* list = decoded.find("announce-list");
    const auto* row0 = list->asList().at(0).asList().data();
    EXPECT_EQ(row0[0].toString(), "http://tracker.example.com/announce");
    EXPECT_EQ(row0[1].toString(), "https://mirror.example.com/announce"); // не тронута
    EXPECT_EQ(decoded.find("comment")->toString(),
              "https://kinozalnew.example/details.php?id=12345"); // содержимое сохранено
    // info-секция не повреждена
    EXPECT_EQ(decoded.find("info")->find("name")->toString(), "test.mkv");
    EXPECT_FALSE(repairTouchesInfo(rep.fixedBytes, rep.fixes));
}

TEST(Bencode, RepairUnrepairable)
{
    RepairResult rep;
    std::string error;
    EXPECT_FALSE(repair("d1:a5:xx2:yy", rep, &error)); // мусор с дыркой
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(rep.fixedBytes.empty());
}

TEST(Bencode, RepairInsideInfoRejected)
{
    // Порча внутри info: 14 байт вырезаны из pieces, длина осталась 20.
    std::string s = makeSampleTorrent();
    const std::string oldStr = "20:" + std::string(20, 'x');
    s = replaceAll(s, oldStr, "20:" + std::string(6, 'x'), 1);
    // После 6-го 'x' идёт 'e' (конец info-словаря) — честная порча границы.

    RepairResult rep;
    std::string error;
    EXPECT_FALSE(repair(s, rep, &error));
    EXPECT_TRUE(rep.fixedBytes.empty());
    EXPECT_TRUE(rep.fixes.empty());

}

TEST(Bencode, RepairTrackerCascade)
{
    // Реальный паттерн: announce-list из нескольких трекеров, у каждого
    // вырезан scheme «http:»/«https:» (длина не пересчитана).
    const Value root = Value::dictionary({
        {"announce", Value::string("http://a.example.com/announce")},
        {"announce-list",
         Value::list({Value::list(
             {Value::string("http://a.example.com/announce"),
              Value::string("http://b.example.com/announce"),
              Value::string("https://c.example.com/announce"),
              Value::string("https://d.example.com/announce"),
              Value::string("http://e.example.com/announce")})})},
        {"comment", Value::string("https://kinozal.example/details.php?id=1224024")},
        {"info",
         Value::dictionary({{"name", Value::string("test.mkv")},
                            {"piece length", Value::integer(16384)},
                            {"pieces", Value::string(std::string(20, 'x'))},
                            {"private", Value::integer(1)}})},
    });
    std::string s = encode(root);
    s = replaceAll(s, "http://", "//", 4);
    s = replaceAll(s, "https://", "//", 3);

    Value out;
    std::string error;
    ASSERT_FALSE(decode(s, out, &error)); // строго — уже не декодируется

    RepairResult rep;
    ASSERT_TRUE(repair(s, rep, &error)) << error;
    ASSERT_EQ(rep.fixes.size(), 7u); // 5 URL в списке + announce + comment

    Value decoded;
    ASSERT_TRUE(decode(rep.fixedBytes, decoded, &error)) << error;
    const auto* list = decoded.find("announce-list");
    const auto* row0 = list->asList().at(0).asList().data();
    EXPECT_EQ(row0[0].toString(), "http://a.example.com/announce");
    EXPECT_EQ(row0[1].toString(), "http://b.example.com/announce");
    EXPECT_EQ(row0[2].toString(), "https://c.example.com/announce");
    EXPECT_EQ(row0[3].toString(), "https://d.example.com/announce");
    EXPECT_EQ(row0[4].toString(), "http://e.example.com/announce");
    EXPECT_EQ(decoded.find("announce")->toString(), "http://a.example.com/announce");
    EXPECT_EQ(decoded.find("comment")->toString(),
              "https://kinozal.example/details.php?id=1224024"); // не тронута
    EXPECT_FALSE(repairTouchesInfo(rep.fixedBytes, rep.fixes));
}

TEST(Bencode, RepairCommentIdConfusion)
{
    // Длина префикса в comment рассчитана под короткий id, а в контенте —
    // длинный (длина не пересчитана после правки).
    std::string s = makeSampleTorrent();
    s = replaceAll(s, "id=12345", "id=1234567890", 1);

    Value out;
    std::string error;
    ASSERT_FALSE(decode(s, out, &error));

    RepairResult rep;
    ASSERT_TRUE(repair(s, rep, &error)) << error;
    ASSERT_EQ(rep.fixes.size(), 1u);

    Value decoded;
    ASSERT_TRUE(decode(rep.fixedBytes, decoded, &error)) << error;
    EXPECT_EQ(decoded.find("comment")->toString(),
              "https://kinozal.example/details.php?id=1234567890");
    // info-секция не повреждена
    EXPECT_EQ(decoded.find("info")->find("name")->toString(), "test.mkv");
    EXPECT_FALSE(repairTouchesInfo(rep.fixedBytes, rep.fixes));
}

TEST(Bencode, RepairTruncatedFile)
{
    // Физическая трункция: pieces отсутствует, info не закрыта —
    // содержимое не восстановить, repair обязан чисто провалиться (без краха).
    const std::string full = makeSampleTorrent();
    const size_t cut = full.find("6:pieces");
    ASSERT_NE(cut, std::string::npos);
    const std::string s = full.substr(0, cut);

    RepairResult rep;
    std::string error;
    EXPECT_FALSE(repair(s, rep, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(rep.fixedBytes.empty());
}

TEST(Bencode, RejectsDuplicateEmptyKeysAndDeepNesting)
{
    Value value;
    EXPECT_FALSE(decode("d0:i1e0:i2ee", value));
    EXPECT_FALSE(decode("d1:bi1e1:ai2ee", value));
    EXPECT_FALSE(decode(std::string(200, 'l') + std::string(200, 'e'), value));
    EXPECT_FALSE(decode("999999999999999999999999999999999999:abc", value));
}

TEST(Bencode, RepairPreservesHybridInfoAndPieceLayers)
{
    const std::string binary = std::string("\0\xff", 2) + "4:info13:creation date";
    const Value info = Value::dictionary({
        {"file tree", Value::dictionary({{"x", Value::dictionary({
            {"", Value::dictionary({{"length", Value::integer(1)},
                                   {"pieces root", Value::string(std::string(32, 'r'))}})}})}})},
        {"length", Value::integer(1)}, {"meta version", Value::integer(2)},
        {"name", Value::string("x")}, {"piece length", Value::integer(16384)},
        {"pieces", Value::string(std::string(20, 'p'))}});
    const Value layers = Value::dictionary({{std::string(32, 'r'), Value::string(binary)}});
    const std::string original = encode(Value::dictionary({
        {"announce", Value::string("http://tracker.example/announce")},
        {"created by", Value::string("Creator - https://example.org")},
        {"info", info}, {"piece layers", layers},
        {"publisher", Value::string("old.example")},
        {"publisher-url", Value::string("https://old.example/12345")}}));
    std::string corrupt = replaceAll(original, "http://", "//", 1);
    corrupt = replaceAll(corrupt, "https://example.org", "//example.org", 1);
    corrupt = replaceAll(corrupt, "old.example", "longer.example", 2);
    RepairResult repaired;
    std::string error;
    ASSERT_TRUE(repair(corrupt, repaired, &error)) << error;
    Value decoded;
    ASSERT_TRUE(decode(repaired.fixedBytes, decoded));
    EXPECT_EQ(encode(*decoded.find("info")), encode(info));
    EXPECT_EQ(encode(*decoded.find("piece layers")), encode(layers));
    EXPECT_NE(repaired.fixedBytes.find(encode(info)), std::string::npos);
    EXPECT_NE(repaired.fixedBytes.find(encode(layers)), std::string::npos);
    EXPECT_EQ(decoded.find("publisher")->toString(), "longer.example");
    EXPECT_EQ(decoded.find("created by")->toString(), "Creator - //example.org");
}

TEST(Bencode, RepairRefusesBrokenBinaryPayloadAfterMetadata)
{
    std::string corrupt = replaceAll(makeSampleTorrent(), "http://", "//", 2);
    corrupt = replaceAll(corrupt, std::string(20, 'x'), "short", 1);
    RepairResult repaired;
    EXPECT_FALSE(repair(corrupt, repaired));
    EXPECT_TRUE(repaired.fixedBytes.empty());
    EXPECT_TRUE(repaired.fixes.empty());
}
