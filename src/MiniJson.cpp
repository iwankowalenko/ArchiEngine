#include "MiniJson.h"

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <sstream>

namespace archi
{
    JsonValue JsonValue::MakeNull()
    {
        return JsonValue{};
    }

    JsonValue JsonValue::MakeBool(bool value)
    {
        JsonValue result{};
        result.type = Type::Bool;
        result.boolValue = value;
        return result;
    }

    JsonValue JsonValue::MakeNumber(double value)
    {
        JsonValue result{};
        result.type = Type::Number;
        result.numberValue = value;
        return result;
    }

    JsonValue JsonValue::MakeString(std::string value)
    {
        JsonValue result{};
        result.type = Type::String;
        result.stringValue = std::move(value);
        return result;
    }

    JsonValue JsonValue::MakeArray()
    {
        JsonValue result{};
        result.type = Type::Array;
        return result;
    }

    JsonValue JsonValue::MakeObject()
    {
        JsonValue result{};
        result.type = Type::Object;
        return result;
    }

    const JsonValue* JsonValue::Find(const std::string& key) const
    {
        if (!IsObject())
            return nullptr;
        const auto it = objectValue.find(key);
        return it != objectValue.end() ? &it->second : nullptr;
    }

    JsonValue* JsonValue::Find(const std::string& key)
    {
        if (!IsObject())
            return nullptr;
        const auto it = objectValue.find(key);
        return it != objectValue.end() ? &it->second : nullptr;
    }

    namespace
    {
        class JsonParser final
        {
        public:
            explicit JsonParser(std::string_view text) : m_text(text) {}

            bool Parse(JsonValue& outValue, std::string* outError)
            {
                SkipWhitespace();
                if (!ParseValue(outValue, outError))
                    return false;

                SkipWhitespace();
                if (!IsAtEnd())
                {
                    SetError(outError, "Unexpected trailing characters");
                    return false;
                }

                return true;
            }

        private:
            bool ParseValue(JsonValue& outValue, std::string* outError)
            {
                if (IsAtEnd())
                {
                    SetError(outError, "Unexpected end of JSON");
                    return false;
                }

                switch (Peek())
                {
                case 'n':
                    return ParseKeyword("null", JsonValue::MakeNull(), outValue, outError);
                case 't':
                    return ParseKeyword("true", JsonValue::MakeBool(true), outValue, outError);
                case 'f':
                    return ParseKeyword("false", JsonValue::MakeBool(false), outValue, outError);
                case '"':
                    return ParseStringValue(outValue, outError);
                case '[':
                    return ParseArray(outValue, outError);
                case '{':
                    return ParseObject(outValue, outError);
                default:
                    if (Peek() == '-' || std::isdigit(static_cast<unsigned char>(Peek())))
                        return ParseNumber(outValue, outError);
                    SetError(outError, "Unexpected token");
                    return false;
                }
            }

            bool ParseKeyword(std::string_view keyword, JsonValue replacement, JsonValue& outValue, std::string* outError)
            {
                if (m_text.substr(m_pos, keyword.size()) != keyword)
                {
                    SetError(outError, "Invalid keyword");
                    return false;
                }

                m_pos += keyword.size();
                outValue = std::move(replacement);
                return true;
            }

            bool ParseStringValue(JsonValue& outValue, std::string* outError)
            {
                std::string value;
                if (!ParseString(value, outError))
                    return false;

                outValue = JsonValue::MakeString(std::move(value));
                return true;
            }

            bool ParseString(std::string& outValue, std::string* outError)
            {
                if (!Consume('"'))
                {
                    SetError(outError, "Expected string");
                    return false;
                }

                std::string result;
                while (!IsAtEnd())
                {
                    const char ch = Advance();
                    if (ch == '"')
                    {
                        outValue = std::move(result);
                        return true;
                    }

                    if (ch == '\\')
                    {
                        if (IsAtEnd())
                        {
                            SetError(outError, "Invalid escape sequence");
                            return false;
                        }

                        switch (const char escaped = Advance())
                        {
                        case '"':
                        case '\\':
                        case '/':
                            result.push_back(escaped);
                            break;
                        case 'b':
                            result.push_back('\b');
                            break;
                        case 'f':
                            result.push_back('\f');
                            break;
                        case 'n':
                            result.push_back('\n');
                            break;
                        case 'r':
                            result.push_back('\r');
                            break;
                        case 't':
                            result.push_back('\t');
                            break;
                        default:
                            SetError(outError, "Unsupported escape sequence");
                            return false;
                        }
                        continue;
                    }

                    result.push_back(ch);
                }

                SetError(outError, "Unterminated string");
                return false;
            }

            bool ParseNumber(JsonValue& outValue, std::string* outError)
            {
                const std::size_t start = m_pos;

                if (Peek() == '-')
                    ++m_pos;

                if (IsAtEnd())
                {
                    SetError(outError, "Invalid number");
                    return false;
                }

                if (Peek() == '0')
                {
                    ++m_pos;
                }
                else
                {
                    if (!std::isdigit(static_cast<unsigned char>(Peek())))
                    {
                        SetError(outError, "Invalid number");
                        return false;
                    }

                    while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek())))
                        ++m_pos;
                }

                if (!IsAtEnd() && Peek() == '.')
                {
                    ++m_pos;
                    if (IsAtEnd() || !std::isdigit(static_cast<unsigned char>(Peek())))
                    {
                        SetError(outError, "Invalid number");
                        return false;
                    }

                    while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek())))
                        ++m_pos;
                }

                if (!IsAtEnd() && (Peek() == 'e' || Peek() == 'E'))
                {
                    ++m_pos;
                    if (!IsAtEnd() && (Peek() == '+' || Peek() == '-'))
                        ++m_pos;
                    if (IsAtEnd() || !std::isdigit(static_cast<unsigned char>(Peek())))
                    {
                        SetError(outError, "Invalid number exponent");
                        return false;
                    }
                    while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek())))
                        ++m_pos;
                }

                const std::string numberText(m_text.substr(start, m_pos - start));
                char* endPtr = nullptr;
                const double number = std::strtod(numberText.c_str(), &endPtr);
                if (!endPtr || *endPtr != '\0')
                {
                    SetError(outError, "Failed to parse number");
                    return false;
                }

                outValue = JsonValue::MakeNumber(number);
                return true;
            }

            bool ParseArray(JsonValue& outValue, std::string* outError)
            {
                if (!Consume('['))
                {
                    SetError(outError, "Expected array");
                    return false;
                }

                JsonValue array = JsonValue::MakeArray();
                SkipWhitespace();
                if (Consume(']'))
                {
                    outValue = std::move(array);
                    return true;
                }

                while (true)
                {
                    JsonValue element{};
                    SkipWhitespace();
                    if (!ParseValue(element, outError))
                        return false;
                    array.arrayValue.push_back(std::move(element));

                    SkipWhitespace();
                    if (Consume(']'))
                    {
                        outValue = std::move(array);
                        return true;
                    }

                    if (!Consume(','))
                    {
                        SetError(outError, "Expected ',' or ']'");
                        return false;
                    }
                    SkipWhitespace();
                }
            }

            bool ParseObject(JsonValue& outValue, std::string* outError)
            {
                if (!Consume('{'))
                {
                    SetError(outError, "Expected object");
                    return false;
                }

                JsonValue object = JsonValue::MakeObject();
                SkipWhitespace();
                if (Consume('}'))
                {
                    outValue = std::move(object);
                    return true;
                }

                while (true)
                {
                    SkipWhitespace();
                    std::string key;
                    if (!ParseString(key, outError))
                        return false;

                    SkipWhitespace();
                    if (!Consume(':'))
                    {
                        SetError(outError, "Expected ':'");
                        return false;
                    }

                    JsonValue value{};
                    SkipWhitespace();
                    if (!ParseValue(value, outError))
                        return false;

                    object.objectValue.emplace(std::move(key), std::move(value));

                    SkipWhitespace();
                    if (Consume('}'))
                    {
                        outValue = std::move(object);
                        return true;
                    }

                    if (!Consume(','))
                    {
                        SetError(outError, "Expected ',' or '}'");
                        return false;
                    }
                }
            }

            void SkipWhitespace()
            {
                while (!IsAtEnd() && std::isspace(static_cast<unsigned char>(Peek())))
                    ++m_pos;
            }

            bool IsAtEnd() const
            {
                return m_pos >= m_text.size();
            }

            char Peek() const
            {
                return IsAtEnd() ? '\0' : m_text[m_pos];
            }

            char Advance()
            {
                return IsAtEnd() ? '\0' : m_text[m_pos++];
            }

            bool Consume(char expected)
            {
                if (Peek() != expected)
                    return false;
                ++m_pos;
                return true;
            }

            void SetError(std::string* outError, std::string message) const
            {
                if (outError)
                {
                    std::ostringstream ss;
                    ss << message << " at position " << m_pos;
                    *outError = ss.str();
                }
            }

        private:
            std::string_view m_text;
            std::size_t m_pos = 0;
        };

        void WriteEscapedString(const std::string& value, std::string& out)
        {
            out.push_back('"');
            for (const char ch : value)
            {
                switch (ch)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\b':
                    out += "\\b";
                    break;
                case '\f':
                    out += "\\f";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    out.push_back(ch);
                    break;
                }
            }
            out.push_back('"');
        }

        void AppendIndent(std::string& out, int level, int indentSize)
        {
            out.append(static_cast<std::size_t>(level * indentSize), ' ');
        }

        void WriteValue(const JsonValue& value, std::string& out, bool pretty, int indentSize, int level)
        {
            switch (value.type)
            {
            case JsonValue::Type::Null:
                out += "null";
                break;
            case JsonValue::Type::Bool:
                out += value.boolValue ? "true" : "false";
                break;
            case JsonValue::Type::Number:
            {
                std::ostringstream ss;
                ss.precision(15);
                ss << value.numberValue;
                out += ss.str();
                break;
            }
            case JsonValue::Type::String:
                WriteEscapedString(value.stringValue, out);
                break;
            case JsonValue::Type::Array:
            {
                out.push_back('[');
                if (!value.arrayValue.empty())
                {
                    if (pretty)
                        out.push_back('\n');

                    for (std::size_t i = 0; i < value.arrayValue.size(); ++i)
                    {
                        if (pretty)
                            AppendIndent(out, level + 1, indentSize);
                        WriteValue(value.arrayValue[i], out, pretty, indentSize, level + 1);
                        if (i + 1 < value.arrayValue.size())
                            out.push_back(',');
                        if (pretty)
                            out.push_back('\n');
                    }

                    if (pretty)
                        AppendIndent(out, level, indentSize);
                }
                out.push_back(']');
                break;
            }
            case JsonValue::Type::Object:
            {
                out.push_back('{');
                if (!value.objectValue.empty())
                {
                    if (pretty)
                        out.push_back('\n');

                    std::size_t index = 0;
                    for (const auto& [key, child] : value.objectValue)
                    {
                        if (pretty)
                            AppendIndent(out, level + 1, indentSize);
                        WriteEscapedString(key, out);
                        out += pretty ? ": " : ":";
                        WriteValue(child, out, pretty, indentSize, level + 1);
                        if (++index < value.objectValue.size())
                            out.push_back(',');
                        if (pretty)
                            out.push_back('\n');
                    }

                    if (pretty)
                        AppendIndent(out, level, indentSize);
                }
                out.push_back('}');
                break;
            }
            }
        }
    }

    bool ParseJson(std::string_view text, JsonValue& outValue, std::string* outError)
    {
        JsonParser parser(text);
        return parser.Parse(outValue, outError);
    }

    std::string WriteJson(const JsonValue& value, bool pretty, int indentSize)
    {
        std::string result;
        WriteValue(value, result, pretty, indentSize, 0);
        if (pretty)
            result.push_back('\n');
        return result;
    }
}
