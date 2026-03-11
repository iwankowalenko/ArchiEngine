#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace archi
{
    struct JsonValue
    {
        enum class Type
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object
        };

        using Array = std::vector<JsonValue>;
        using Object = std::map<std::string, JsonValue>;

        Type type = Type::Null;
        bool boolValue = false;
        double numberValue = 0.0;
        std::string stringValue{};
        Array arrayValue{};
        Object objectValue{};

        static JsonValue MakeNull();
        static JsonValue MakeBool(bool value);
        static JsonValue MakeNumber(double value);
        static JsonValue MakeString(std::string value);
        static JsonValue MakeArray();
        static JsonValue MakeObject();

        bool IsNull() const { return type == Type::Null; }
        bool IsBool() const { return type == Type::Bool; }
        bool IsNumber() const { return type == Type::Number; }
        bool IsString() const { return type == Type::String; }
        bool IsArray() const { return type == Type::Array; }
        bool IsObject() const { return type == Type::Object; }

        const JsonValue* Find(const std::string& key) const;
        JsonValue* Find(const std::string& key);
    };

    bool ParseJson(std::string_view text, JsonValue& outValue, std::string* outError = nullptr);
    std::string WriteJson(const JsonValue& value, bool pretty = true, int indentSize = 2);
}
