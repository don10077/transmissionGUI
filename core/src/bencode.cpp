#include "tgui/bencode.h"

#include <algorithm>
#include <limits>

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

    size_t depth = 0;

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
        if (depth >= 128)
            return fail("nesting limit exceeded");
        struct DepthGuard {
            size_t& depth;
            explicit DepthGuard(size_t& value) : depth(value) { ++depth; }
            ~DepthGuard() { --depth; }
        } guard(depth);
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
                    if (!parseString(s))
                        return false;
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
            if (!items.empty() && !(lastKey < key))
                return fail("dictionary keys must be unique and sorted");
            Value value;
            if (!parse(value))
                return false;
            lastKey = std::move(key);
            items.emplace_back(lastKey, std::move(value));
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

namespace {

/** Восстанавливает только текстовые поля вне info, не меняя бинарные хеши. */
struct MetadataRepair {
    const std::string& input;
    RepairResult result;
    size_t pos = 0;

    /** Читает заголовок строки с проверкой переполнения. */
    bool header(size_t at, size_t& content, size_t& length) const
    {
        length = 0;
        const size_t begin = at;
        while (at < input.size() && input[at] >= '0' && input[at] <= '9') {
            const size_t digit = static_cast<size_t>(input[at++] - '0');
            if (length > (std::numeric_limits<size_t>::max() - digit) / 10)
                return false;
            length = length * 10 + digit;
        }
        if (at == begin || at == input.size() || input[at] != ':' ||
            (at - begin > 1 && input[begin] == '0'))
            return false;
        content = at + 1;
        return true;
    }

    /** Распознаёт URL, включая URL с вырезанной схемой. */
    bool url(size_t at) const
    {
        for (const char* prefix : {"//", "http://", "https://", "udp://", "wss://", "ws://", "ftp://"})
            if (input.compare(at, std::char_traits<char>::length(prefix), prefix) == 0)
                return true;
        return false;
    }

    /** Распознаёт следующее известное поле корневого словаря. */
    bool rootKey(size_t at) const
    {
        size_t content = 0, length = 0;
        if (!header(at, content, length) || length > 13 || length > input.size() - content)
            return false;
        const std::string key = input.substr(content, length);
        for (const char* known : {"announce", "announce-list", "comment", "created by",
                                 "creation date", "encoding", "info", "nodes", "piece layers",
                                 "publisher", "publisher-url", "url-list", "httpseeds"})
            if (key == known)
                return true;
        return false;
    }

    /** Проверяет границу текста, не заходя в бинарные поля. */
    bool boundary(size_t at, size_t trackerDepth) const
    {
        if (trackerDepth == 0)
            return rootKey(at) || (at + 1 == input.size() && input[at] == 'e');
        const size_t begin = at;
        while (at < input.size() && input[at] == 'e')
            ++at;
        const size_t closed = at - begin;
        if (rootKey(at))
            return closed == trackerDepth;
        if (at == input.size())
            return closed == trackerDepth + 1;
        if (input[at] == 'l') {
            if (closed != 1)
                return false;
            ++at;
        } else if (closed != 0) {
            return false;
        }
        size_t content = 0, length = 0;
        return header(at, content, length) && url(content);
    }

    /** Пересчитывает длину текста; схему восстанавливает по числу пропавших байт. */
    bool text(size_t trackerDepth)
    {
        const size_t begin = pos;
        size_t content = 0, length = 0;
        if (!header(pos, content, length))
            return false;
        size_t end = std::string::npos;
        if (length <= input.size() - content && boundary(content + length, trackerDepth))
            end = content + length;
        const size_t limit = content + std::min(size_t(16384), input.size() - content);
        for (size_t at = content; end == std::string::npos && at < limit; ++at) {
            if (boundary(at, trackerDepth)) {
                end = at;
                break;
            }
            const unsigned char byte = static_cast<unsigned char>(input[at]);
            if (byte < 32 && byte != '\t' && byte != '\n' && byte != '\r')
                return false;
        }
        if (end == std::string::npos)
            return false;
        std::string value = input.substr(content, end - content);
        if (input.compare(content, 2, "//") == 0 && length > value.size()) {
            const size_t missing = length - value.size();
            if (missing == 5)
                value.insert(0, "http:");
            else if (missing == 6)
                value.insert(0, "https:");
            else
                return false;
        }
        const std::string fixed = std::to_string(value.size()) + ":" + value;
        if (fixed != input.substr(begin, end - begin)) {
            result.fixes.push_back({begin, std::to_string(length), std::to_string(value.size()),
                                    "repaired text metadata at byte " + std::to_string(begin)});
        }
        result.fixedBytes += fixed;
        pos = end;
        return true;
    }

    /** Копирует списки трекеров, исправляя только содержащиеся в них URL. */
    bool trackers(size_t depth = 0)
    {
        if (pos >= input.size() || depth > 8)
            return false;
        if (input[pos] != 'l')
            return text(depth);
        result.fixedBytes += input[pos++];
        while (pos < input.size() && input[pos] != 'e')
            if (!trackers(depth + 1))
                return false;
        if (pos == input.size())
            return false;
        result.fixedBytes += input[pos++];
        return true;
    }

    /** Обходит корневые поля; info и piece layers копируются строго побайтово. */
    bool run()
    {
        if (input.empty() || input[pos++] != 'd')
            return false;
        result.fixedBytes = "d";
        while (pos < input.size() && input[pos] != 'e') {
            Parser keyParser{input.data(), input.size(), pos, ""};
            std::string key;
            if (!keyParser.parseString(key))
                return false;
            result.fixedBytes.append(input, pos, keyParser.pos - pos);
            pos = keyParser.pos;
            if (key == "announce-list" || key == "url-list" || key == "httpseeds") {
                if (!trackers())
                    return false;
            } else if (key == "announce" || key == "comment" || key == "created by" ||
                       key == "publisher" || key == "publisher-url") {
                if (!text(0))
                    return false;
            } else {
                Parser parser{input.data(), input.size(), pos, ""};
                Value value;
                if (!parser.parse(value))
                    return false;
                result.fixedBytes.append(input, pos, parser.pos - pos);
                pos = parser.pos;
            }
        }
        if (pos + 1 != input.size() || input[pos] != 'e')
            return false;
        result.fixedBytes += 'e';
        Value value;
        return decode(result.fixedBytes, value) && value.find("info") &&
               value.find("info")->type() == Value::Type::Dictionary;
    }
};

} // namespace

bool repair(const std::string& input, RepairResult& out, std::string* error)
{
    out = {};
    if (error)
        error->clear();
    Value value;
    if (decode(input, value) && value.find("info") &&
        value.find("info")->type() == Value::Type::Dictionary) {
        out.fixedBytes = input;
        return true;
    }
    MetadataRepair parser{input, {}, 0};
    if (!parser.run()) {
        if (error)
            *error = "Unable to repair text metadata without changing torrent info (byte " +
                     std::to_string(parser.pos) + ")";
        return false;
    }
    out = std::move(parser.result);
    return true;
}

bool repairTouchesInfo(const std::string&, const std::vector<RepairFix>&)
{
    // repair() никогда не меняет info; позиции исправлений относятся к оригиналу.
    return false;
}

} // namespace tgui::bencode
