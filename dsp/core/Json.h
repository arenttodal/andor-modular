#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace andor::modular::json
{

/**
    A small JSON value for the patch format (no JUCE in the kernel: patches are read and
    written by the headless tools, the tests and the plugin with the same code).

    Objects keep their insertion order, so a document written twice is byte-identical
    (deterministic diffs, content hashes). The parser is defensive: depth, size and number
    limits, never throws, reports the first error with its byte offset (fuzzed in the tests).
*/
class Value
{
public:
    enum class Type : std::uint8_t { null, boolean, number, string, array, object };
    using Member = std::pair<std::string, Value>;

    Value() = default;
    Value (std::nullptr_t) {}
    Value (bool b) : type (Type::boolean), boolValue (b) {}
    Value (double d) : type (Type::number), number (d) {}
    Value (int i) : type (Type::number), number (static_cast<double> (i)) {}
    Value (std::int64_t i) : type (Type::number), number (static_cast<double> (i)) {}
    Value (const char* s) : type (Type::string), text (s) {}
    Value (std::string s) : type (Type::string), text (std::move (s)) {}

    static Value array() { Value v; v.type = Type::array; return v; }
    static Value object() { Value v; v.type = Type::object; return v; }

    Type kind() const noexcept { return type; }
    bool isNull() const noexcept { return type == Type::null; }
    bool isBool() const noexcept { return type == Type::boolean; }
    bool isNumber() const noexcept { return type == Type::number; }
    bool isString() const noexcept { return type == Type::string; }
    bool isArray() const noexcept { return type == Type::array; }
    bool isObject() const noexcept { return type == Type::object; }

    double asNumber (double fallback = 0.0) const noexcept { return type == Type::number ? number : fallback; }
    int asInt (int fallback = 0) const noexcept;
    bool asBool (bool fallback = false) const noexcept { return type == Type::boolean ? boolValue : fallback; }
    const std::string& asString() const noexcept;
    std::string asString (const std::string& fallback) const { return type == Type::string ? text : fallback; }

    // arrays
    std::size_t size() const noexcept { return type == Type::array ? items.size() : (type == Type::object ? members.size() : 0); }
    const Value& operator[] (std::size_t i) const noexcept;
    Value& push (Value v);
    const std::vector<Value>& elements() const noexcept { return items; }

    // objects
    const Value& operator[] (const std::string& key) const noexcept;
    const Value* find (const std::string& key) const noexcept;
    bool has (const std::string& key) const noexcept { return find (key) != nullptr; }
    /** Sets (replacing an existing key in place, else appending). Turns null into an object. */
    Value& set (const std::string& key, Value v);
    const std::vector<Member>& entries() const noexcept { return members; }

    double get (const std::string& key, double fallback) const noexcept { return (*this)[key].asNumber (fallback); }
    int getInt (const std::string& key, int fallback) const noexcept { return (*this)[key].asInt (fallback); }
    bool getBool (const std::string& key, bool fallback) const noexcept { return (*this)[key].asBool (fallback); }
    std::string getString (const std::string& key, const std::string& fallback = {}) const { return (*this)[key].asString (fallback); }

    static const Value& nullValue() noexcept;

private:
    Type type = Type::null;
    bool boolValue = false;
    double number = 0.0;
    std::string text;
    std::vector<Value> items;
    std::vector<Member> members;
};

struct ParseResult
{
    Value value;
    bool ok = false;
    std::string error;
    std::size_t errorOffset = 0;
};

struct ParseLimits
{
    int maxDepth = 64;
    std::size_t maxBytes = 64u * 1024u * 1024u;
    std::size_t maxStringBytes = 16u * 1024u * 1024u;
};

ParseResult parse (const std::string& text, const ParseLimits& limits = {});

/** Compact (indent < 0) or pretty (indent spaces) text; numbers in the shortest round-trip form. */
std::string write (const Value& v, int indent = 2);

/** A value derived through a curve (log / pow, an interpolation), as a document stores it:
    rounded to `digits` significant decimal digits. The last digits of such a result differ
    between platforms (their maths libraries, fused multiply-add); a stored value must not,
    so the same edit writes the same patch everywhere. `scale` (the size of the value's range,
    when known): a result that small beside it (under scale * 10^-digits) is the residue of a
    value that is zero, and is stored as zero. Message thread (it allocates). */
double roundSignificant (double value, int digits = 12, double scale = 0.0);

} // namespace andor::modular::json
