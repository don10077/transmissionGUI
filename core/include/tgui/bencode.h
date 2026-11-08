#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tgui::bencode {

// Zначение bencode: целое, строка, список, словарь (аналог transgui bencode.pas).
class Value {
public:
    enum class Type { Integer, String, List, Dictionary };
    using List = std::vector<Value>;
    using Dict = std::vector<std::pair<std::string, Value>>;

    Value() = default; // 0 (befEmpty)

    static Value integer(std::int64_t v);
    static Value string(std::string v);
    static Value list(List items);
    static Value dictionary(Dict items);

    Type type() const noexcept { return m_type; }
    std::int64_t toInt() const noexcept { return m_int; }
    const std::string& toString() const noexcept { return m_string; }
    const List& asList() const noexcept { return m_list; }
    const Dict& asDict() const noexcept { return m_dict; }

    // Поиск по словарю (nullptr, если словарь не тот или ключ отсутствует).
    const Value* find(const std::string& key) const;

private:
    Type m_type = Type::Integer;
    std::int64_t m_int = 0;
    std::string m_string;
    List m_list;
    Dict m_dict;
};

// Кодирование. Ключи словаря кодируются в порядке asDict() —
// для валидного bittorrent-файла они должны быть отсортированы по возрастанию.
std::string encode(const Value& value);

// Декодирование с проверкой границ. При ошибке возвращает false и (если error
// не null) текстовую причину. Требует: уникальные ключи, отсортированные по
// возрастанию, отсутствие лишних байт в конце.
bool decode(const std::string& input, Value& out, std::string* error = nullptr);

} // namespace tgui::bencode