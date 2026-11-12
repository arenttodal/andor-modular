#include "core/Json.h"

#include <locale>
#include <sstream>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace andor::modular::json
{

namespace
{
    // Numbers are read and written in the classic "C" locale whatever the host set (a DAW may
    // run with a decimal comma), through the streams: <charconv>'s floating-point from_chars /
    // to_chars are missing from Apple's libc++ at this deployment target. Message thread only.
    bool parseDouble (const char* text, std::size_t length, double& out)
    {
        std::istringstream in (std::string (text, length));
        in.imbue (std::locale::classic());
        in >> out;
        return ! in.fail() && (in.eof() || in.peek() == std::char_traits<char>::eof());
    }

    std::string formatDouble (double value, int significantDigits)
    {
        std::ostringstream o;
        o.imbue (std::locale::classic());
        o.precision (significantDigits);
        o << value;
        return o.str();
    }
}

namespace
{
    const std::string emptyString;
}

const Value& Value::nullValue() noexcept
{
    static const Value v;
    return v;
}

int Value::asInt (int fallback) const noexcept
{
    if (type != Type::number || ! std::isfinite (number))
        return fallback;
    if (number > 2.0e9 || number < -2.0e9)
        return fallback;
    return static_cast<int> (std::lround (number));
}

const std::string& Value::asString() const noexcept { return type == Type::string ? text : emptyString; }

const Value& Value::operator[] (std::size_t i) const noexcept
{
    return type == Type::array && i < items.size() ? items[i] : nullValue();
}

Value& Value::push (Value v)
{
    if (type != Type::array)
    {
        *this = array();
    }
    items.push_back (std::move (v));
    return items.back();
}

const Value* Value::find (const std::string& key) const noexcept
{
    if (type != Type::object)
        return nullptr;
    for (const auto& m : members)
        if (m.first == key)
            return &m.second;
    return nullptr;
}

const Value& Value::operator[] (const std::string& key) const noexcept
{
    const auto* v = find (key);
    return v != nullptr ? *v : nullValue();
}

Value& Value::set (const std::string& key, Value v)
{
    if (type != Type::object)
        *this = object();
    for (auto& m : members)
        if (m.first == key)
        {
            m.second = std::move (v);
            return m.second;
        }
    members.emplace_back (key, std::move (v));
    return members.back().second;
}

//==============================================================================
namespace
{
    struct Parser
    {
        const std::string& s;
        const ParseLimits& limits;
        std::size_t pos = 0;
        std::string error;

        bool fail (const char* message)
        {
            if (error.empty())
                error = message;
            return false;
        }

        void skipSpace() noexcept
        {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t'))
                ++pos;
        }

        bool literal (const char* word)
        {
            const auto n = std::strlen (word);
            if (s.compare (pos, n, word) != 0)
                return fail ("invalid literal");
            pos += n;
            return true;
        }

        static void appendUtf8 (std::string& out, std::uint32_t cp)
        {
            if (cp < 0x80)
                out += static_cast<char> (cp);
            else if (cp < 0x800)
            {
                out += static_cast<char> (0xC0 | (cp >> 6));
                out += static_cast<char> (0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                out += static_cast<char> (0xE0 | (cp >> 12));
                out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char> (0x80 | (cp & 0x3F));
            }
            else
            {
                out += static_cast<char> (0xF0 | (cp >> 18));
                out += static_cast<char> (0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char> (0x80 | (cp & 0x3F));
            }
        }

        bool hex4 (std::uint32_t& out)
        {
            if (pos + 4 > s.size())
                return fail ("truncated \\u escape");
            out = 0;
            for (int i = 0; i < 4; ++i)
            {
                const char c = s[pos++];
                out <<= 4;
                if (c >= '0' && c <= '9')
                    out |= static_cast<std::uint32_t> (c - '0');
                else if (c >= 'a' && c <= 'f')
                    out |= static_cast<std::uint32_t> (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F')
                    out |= static_cast<std::uint32_t> (c - 'A' + 10);
                else
                    return fail ("bad hex digit");
            }
            return true;
        }

        bool string (std::string& out)
        {
            ++pos;   // opening quote
            while (true)
            {
                if (pos >= s.size())
                    return fail ("unterminated string");
                if (out.size() > limits.maxStringBytes)
                    return fail ("string too long");
                const char c = s[pos++];
                if (c == '"')
                    return true;
                if (static_cast<unsigned char> (c) < 0x20)
                    return fail ("control character in string");
                if (c != '\\')
                {
                    out += c;
                    continue;
                }
                if (pos >= s.size())
                    return fail ("truncated escape");
                const char e = s[pos++];
                switch (e)
                {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u':
                    {
                        std::uint32_t cp = 0;
                        if (! hex4 (cp))
                            return false;
                        if (cp >= 0xD800 && cp < 0xDC00 && pos + 1 < s.size() && s[pos] == '\\' && s[pos + 1] == 'u')
                        {
                            pos += 2;
                            std::uint32_t low = 0;
                            if (! hex4 (low))
                                return false;
                            if (low >= 0xDC00 && low < 0xE000)
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            else
                                cp = 0xFFFD;
                        }
                        else if (cp >= 0xD800 && cp < 0xE000)
                            cp = 0xFFFD;
                        appendUtf8 (out, cp);
                        break;
                    }
                    default: return fail ("bad escape");
                }
            }
        }

        bool number (double& out)
        {
            const std::size_t start = pos;
            if (pos < s.size() && s[pos] == '-')
                ++pos;
            while (pos < s.size() && ((s[pos] >= '0' && s[pos] <= '9') || s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E' || s[pos] == '+' || s[pos] == '-'))
                ++pos;
            if (pos == start || pos - start > 64)
                return fail ("bad number");
            if (! parseDouble (s.data() + start, pos - start, out) || ! std::isfinite (out))
                return fail ("bad number");
            return true;
        }

        bool value (Value& out, int depth)
        {
            if (depth > limits.maxDepth)
                return fail ("nesting too deep");
            skipSpace();
            if (pos >= s.size())
                return fail ("unexpected end");
            const char c = s[pos];
            if (c == 'n')
            {
                out = Value();
                return literal ("null");
            }
            if (c == 't')
            {
                out = Value (true);
                return literal ("true");
            }
            if (c == 'f')
            {
                out = Value (false);
                return literal ("false");
            }
            if (c == '"')
            {
                std::string text;
                if (! string (text))
                    return false;
                out = Value (std::move (text));
                return true;
            }
            if (c == '[')
            {
                ++pos;
                out = Value::array();
                skipSpace();
                if (pos < s.size() && s[pos] == ']')
                {
                    ++pos;
                    return true;
                }
                while (true)
                {
                    Value item;
                    if (! value (item, depth + 1))
                        return false;
                    out.push (std::move (item));
                    skipSpace();
                    if (pos >= s.size())
                        return fail ("unterminated array");
                    if (s[pos] == ',')
                    {
                        ++pos;
                        continue;
                    }
                    if (s[pos] == ']')
                    {
                        ++pos;
                        return true;
                    }
                    return fail ("expected , or ]");
                }
            }
            if (c == '{')
            {
                ++pos;
                out = Value::object();
                skipSpace();
                if (pos < s.size() && s[pos] == '}')
                {
                    ++pos;
                    return true;
                }
                while (true)
                {
                    skipSpace();
                    if (pos >= s.size() || s[pos] != '"')
                        return fail ("expected key");
                    std::string key;
                    if (! string (key))
                        return false;
                    skipSpace();
                    if (pos >= s.size() || s[pos] != ':')
                        return fail ("expected :");
                    ++pos;
                    Value item;
                    if (! value (item, depth + 1))
                        return false;
                    out.set (key, std::move (item));
                    skipSpace();
                    if (pos >= s.size())
                        return fail ("unterminated object");
                    if (s[pos] == ',')
                    {
                        ++pos;
                        continue;
                    }
                    if (s[pos] == '}')
                    {
                        ++pos;
                        return true;
                    }
                    return fail ("expected , or }");
                }
            }
            if (c == '-' || (c >= '0' && c <= '9'))
            {
                double d = 0.0;
                if (! number (d))
                    return false;
                out = Value (d);
                return true;
            }
            return fail ("unexpected character");
        }
    };

    void writeString (std::string& out, const std::string& text)
    {
        out += '"';
        for (const char ch : text)
        {
            const auto c = static_cast<unsigned char> (ch);
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        std::snprintf (buf, sizeof buf, "\\u%04x", c);
                        out += buf;
                    }
                    else
                        out += ch;
            }
        }
        out += '"';
    }

    void writeNumber (std::string& out, double d)
    {
        if (! std::isfinite (d))
        {
            out += "0";
            return;
        }
        if (d == std::floor (d) && std::abs (d) < 1.0e15)
        {
            char buf[32];
            std::snprintf (buf, sizeof buf, "%lld", static_cast<long long> (d));
            out += buf;
            return;
        }
        // The shortest text that reads back as exactly this value (15, 16 or 17 digits).
        for (const int digits : { 15, 16, 17 })
        {
            const auto text = formatDouble (d, digits);
            double back = 0.0;
            if (digits == 17 || (parseDouble (text.data(), text.size(), back) && back == d))
            {
                out += text;
                return;
            }
        }
    }

    void writeValue (std::string& out, const Value& v, int indent, int level)
    {
        auto newline = [&] (int l)
        {
            if (indent < 0)
                return;
            out += '\n';
            out.append (static_cast<std::size_t> (indent * l), ' ');
        };
        switch (v.kind())
        {
            case Value::Type::null: out += "null"; break;
            case Value::Type::boolean: out += v.asBool() ? "true" : "false"; break;
            case Value::Type::number: writeNumber (out, v.asNumber()); break;
            case Value::Type::string: writeString (out, v.asString()); break;
            case Value::Type::array:
            {
                if (v.size() == 0)
                {
                    out += "[]";
                    break;
                }
                // Short arrays of numbers stay on one line (scene values, positions).
                bool flat = v.size() <= 8;
                for (const auto& e : v.elements())
                    flat = flat && (e.isNumber() || e.isBool() || e.isNull());
                out += '[';
                for (std::size_t i = 0; i < v.size(); ++i)
                {
                    if (i > 0)
                        out += flat || indent < 0 ? (indent < 0 ? "," : ", ") : ",";
                    if (! flat)
                        newline (level + 1);
                    writeValue (out, v[i], indent, level + 1);
                }
                if (! flat)
                    newline (level);
                out += ']';
                break;
            }
            case Value::Type::object:
            {
                if (v.size() == 0)
                {
                    out += "{}";
                    break;
                }
                out += '{';
                bool first = true;
                for (const auto& m : v.entries())
                {
                    if (! first)
                        out += ',';
                    first = false;
                    newline (level + 1);
                    writeString (out, m.first);
                    out += indent < 0 ? ":" : ": ";
                    writeValue (out, m.second, indent, level + 1);
                }
                newline (level);
                out += '}';
                break;
            }
        }
    }
}

ParseResult parse (const std::string& text, const ParseLimits& limits)
{
    ParseResult result;
    if (text.size() > limits.maxBytes)
    {
        result.error = "document too large";
        return result;
    }
    Parser p { text, limits, 0, {} };
    Value v;
    if (p.value (v, 0))
    {
        p.skipSpace();
        if (p.pos != text.size())
            p.fail ("trailing characters");
    }
    if (! p.error.empty())
    {
        result.error = p.error;
        result.errorOffset = p.pos;
        return result;
    }
    result.value = std::move (v);
    result.ok = true;
    return result;
}

std::string write (const Value& v, int indent)
{
    std::string out;
    writeValue (out, v, indent, 0);
    if (indent >= 0)
        out += '\n';
    return out;
}

double roundSignificant (double value, int digits, double scale)
{
    if (value == 0.0 || ! std::isfinite (value))
        return value;
    if (scale > 0.0)
    {
        double floor = scale;
        for (int i = 0; i < digits; ++i)
            floor *= 0.1;
        if (std::abs (value) < floor)
            return 0.0;
    }
    const auto text = formatDouble (value, digits);
    double rounded = value;
    return parseDouble (text.data(), text.size(), rounded) ? rounded : value;
}

} // namespace andor::modular::json
