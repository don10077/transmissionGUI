#include <gtest/gtest.h>

#include "tgui/bencode.h"

using tgui::bencode::Value;
using tgui::bencode::decode;
using tgui::bencode::encode;

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

    // d3:bar4:spam3:baz12:another keyi42ee  (ключи отсортированы)
    const std::string expected = "d3:bar4:spam3:baz12:another keyi42ee";
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