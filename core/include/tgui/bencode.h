#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tgui::bencode {

// Значение bencode: целое, строка, список, словарь.
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

// --- Ремонт повреждённых .torrent ---
//
// Восстанавливаются длины текстовых полей корня и схемы URL трекеров.
// Секция info и остальные бинарные значения копируются побайтово.
// Неизвестная порча (включая повреждённую info) возвращает false.
struct RepairFix {
    size_t pos = 0; // позиция цифр длины в исходном байтовом потоке
    std::string oldLength; // было, например «41»
    std::string newLength; // стало, например «45»
    std::string description; // человекочитаемое пояснение
};
struct RepairResult {
    std::string fixedBytes;
    std::vector<RepairFix> fixes;
};

// Возвращает true, если полученный результат строго декодируется как
// валидный torrent (словарь с «info»). При ошибке — false + текст в error.
bool repair(const std::string& input, RepairResult& out, std::string* error = nullptr);

// Совместимость: repair() не изменяет info, результат всегда false.
bool repairTouchesInfo(const std::string& fixedBytes, const std::vector<RepairFix>& fixes);

} // namespace tgui::bencode
