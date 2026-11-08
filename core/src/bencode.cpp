#include "tgui/bencode.h"

namespace tgui::bencode {

Value Value::integer(std::int64_t v)
{
    Value x;
    x.m_type = Type::Integer;
    x.m_int = v;
    return x;
}

Value Value::string(std::string v)
{
    Value x;
    x.m_type = Type::String;
    x.m_string = std::move(v);
    return x;
}

Value Value::list(List items)
{
    Value x;
    x.m_type = Type::List;
    x.m_list = std::move(items);
    return x;
}

Value Value::dictionary(Dict items)
{
    Value x;
    x.m_type = Type::Dictionary;
    x.m_dict = std::move(items);
    return x;
}

const Value* Value::find(const std::string& key) const
{
    if (m_type != Type::Dictionary)
        return nullptr;
    for (const auto& [k, v] : m_dict)
        if (k == key)
            return &v;
    return nullptr;
}

std::string encode(const Value& value)
{
    std::string out;
    switch (value.type()) {
        case Value::Type::Integer:
            out += 'i';
            out += std::to_string(value.toInt());
            out += 'e';
            break;
        case Value::Type::String: {
            const std::string& s = value.toString();
            out += std::to_string(s.size());
            out += ':';
            out += s;
            break;
        }
        case Value::Type::List: {
            out += 'l';
            for (const auto& item : value.asList())
                out += encode(item);
            out += 'e';
            break;
        }
        case Value::Type::Dictionary: {
            out += 'd';
            for (const auto& [k, v] : value.asDict())
                out += encode(Value::string(k)) + encode(v);
            out += 'e';
            break;
        }
    }
    return out;
}

namespace {

struct Parser {
    const char* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
    std::string error;

    bool fail(const std::string& message)
    {
        if (error.empty())
            error = message + " (pos " + std::to_string(pos) + ")";
        return false;
    }

    bool parse(Value& out)
    {
        if (pos >= size)
            return fail("unexpected end of input");
        switch (data[pos]) {
            case 'i':
                return parseInteger(out);
            case 'l':
                return parseList(out);
            case 'd':
                return parseDict(out);
            default:
                if (data[pos] >= '0' && data[pos] <= '9') {
                    std::string s;
                    if (parseString(s))
                        out = Value::string(std::move(s));
                    return true;
                }
                return fail("invalid bencode: unexpected character '" + std::string(1, data[pos]) + "'");
        }
    }

    bool parseInteger(Value& out)
    {
        ++pos; // 'i'
        size_t start = pos;
        if (pos < size && data[pos] == '-')
            ++pos;
        while (pos < size && data[pos] >= '0' && data[pos] <= '9')
            ++pos;
        if (pos == start || (pos == start + 1 && data[start] == '-'))
            return fail("invalid integer");
        if (pos >= size || data[pos] != 'e')
            return fail("integer is not terminated by 'e'");
        try {
            out = Value::integer(std::stoll(std::string(data + start, pos - start)));
        } catch (const std::exception&) {
            return fail("integer out of range");
        }
        ++pos; // 'e'
        return true;
    }

    bool parseString(std::string& out)
    {
        size_t start = pos;
        while (pos < size && data[pos] >= '0' && data[pos] <= '9')
            ++pos;
        if (pos == start)
            return fail("invalid string length");
        if (pos >= size || data[pos] != ':')
            return fail("string length is not terminated by ':'");
        size_t length;
        try {
            length = std::stoull(std::string(data + start, pos - start));
        } catch (const std::exception&) {
            return fail("string length out of range");
        }
        if (length > size - pos - 1)
            return fail("string exceeds input size");
        out.assign(data + pos + 1, length);
        pos += 1 + length;
        return true;
    }

    bool parseList(Value& out)
    {
        ++pos; // 'l'
        Value::List items;
        while (true) {
            if (pos >= size)
                return fail("unexpected end of list");
            if (data[pos] == 'e') {
                ++pos;
                out = Value::list(std::move(items));
                return true;
            }
            Value item;
            if (!parse(item))
                return false;
            items.push_back(std::move(item));
        }
    }

    bool parseDict(Value& out)
    {
        ++pos; // 'd'
        Value::Dict items;
        std::string lastKey;
        while (true) {
            if (pos >= size)
                return fail("unexpected end of dictionary");
            if (data[pos] == 'e') {
                ++pos;
                out = Value::dictionary(std::move(items));
                return true;
            }
            std::string key;
            if (!parseString(key))
                return false;
            if (!lastKey.empty() && !(lastKey < key))
                return fail("dictionary keys must be unique and sorted");
            Value value;
            if (!parse(value))
                return false;
            lastKey = std::move(key);
            items.emplace_back(std::move(lastKey), std::move(value));
        }
    }
};

} // namespace

bool decode(const std::string& input, Value& out, std::string* error)
{
    Parser p{input.data(), input.size(), 0, ""};
    bool ok = p.parse(out) && p.pos == p.size;
    if (!ok && p.pos != p.size && p.error.empty())
        p.fail("trailing data after bencode value");
    if (error != nullptr)
        *error = ok ? "" : p.error;
    return ok;
}

} // namespace tgui::bencode