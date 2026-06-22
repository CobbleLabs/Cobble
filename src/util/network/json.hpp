#pragma once

#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// Minimal JSON parser/serializer for config sync
// Extended from loader's json.h to support serialization and nested objects

namespace json {

class Value {
public:
    enum Type { Null, Bool, Number, String, Object, Array };

    Value() : type_(Null), bool_(false), number_(0) {}
    Value(bool b) : type_(Bool), bool_(b), number_(0) {}
    Value(int n) : type_(Number), bool_(false), number_(static_cast<double>(n)) {}
    Value(float n) : type_(Number), bool_(false), number_(static_cast<double>(n)) {}
    Value(double n) : type_(Number), bool_(false), number_(n) {}
    Value(const std::string& s) : type_(String), bool_(false), number_(0), string_(s) {}
    Value(const char* s) : type_(String), bool_(false), number_(0), string_(s ? s : "") {}

    Type type() const { return type_; }
    bool is_null() const { return type_ == Null; }
    bool is_bool() const { return type_ == Bool; }
    bool is_number() const { return type_ == Number; }
    bool is_string() const { return type_ == String; }
    bool is_object() const { return type_ == Object; }
    bool is_array() const { return type_ == Array; }

    bool as_bool(bool def = false) const { return type_ == Bool ? bool_ : def; }
    int as_int(int def = 0) const { return type_ == Number ? static_cast<int>(number_) : def; }
    float as_float(float def = 0.0f) const { return type_ == Number ? static_cast<float>(number_) : def; }
    double as_double(double def = 0.0) const { return type_ == Number ? number_ : def; }
    std::string as_string(const std::string& def = "") const { return type_ == String ? string_ : def; }

    // Object access
    Value& operator[](const std::string& key) {
        if (type_ != Object) {
            type_ = Object;
            object_.clear();
        }
        return object_[key];
    }

    const Value& operator[](const std::string& key) const {
        static Value null;
        if (type_ != Object)
            return null;
        auto it = object_.find(key);
        return it != object_.end() ? it->second : null;
    }

    bool has(const std::string& key) const { return type_ == Object && object_.find(key) != object_.end(); }

    const std::map<std::string, Value>& object() const { return object_; }
    std::map<std::string, Value>& object() {
        type_ = Object;
        return object_;
    }

    // Serialization
    std::string serialize() const {
        std::ostringstream ss;
        serialize(ss);
        return ss.str();
    }

    void serialize(std::ostringstream& ss) const {
        switch (type_) {
            case Null:
                ss << "null";
                break;
            case Bool:
                ss << (bool_ ? "true" : "false");
                break;
            case Number:
                if (number_ == static_cast<int>(number_))
                    ss << static_cast<int>(number_);
                else
                    ss << std::fixed << std::setprecision(2) << number_;
                break;
            case String:
                ss << '"';
                for (char c : string_) {
                    switch (c) {
                        case '"':
                            ss << "\\\"";
                            break;
                        case '\\':
                            ss << "\\\\";
                            break;
                        case '\n':
                            ss << "\\n";
                            break;
                        case '\r':
                            ss << "\\r";
                            break;
                        case '\t':
                            ss << "\\t";
                            break;
                        default:
                            ss << c;
                    }
                }
                ss << '"';
                break;
            case Object: {
                ss << '{';
                bool first = true;
                for (const auto& kv : object_) {
                    if (!first)
                        ss << ',';
                    first = false;
                    ss << '"' << kv.first << "\":";
                    kv.second.serialize(ss);
                }
                ss << '}';
                break;
            }
            case Array:
                ss << "[]";  // Not fully implemented, not needed for config
                break;
        }
    }

private:
    Type type_;
    bool bool_;
    double number_;
    std::string string_;
    std::map<std::string, Value> object_;

    friend class Parser;
};

// Color helpers — modules store RGBA as a nested {r,g,b,a} object. These keep
// each module's saveSettings()/loadSettings() free of the repetitive 4-line
// component spelling. readColor() leaves the destination untouched when the
// key is absent, so an old config missing a color preserves the in-memory
// default.
inline void writeColor(Value& parent, const char* key, const float rgba[4]) {
    parent[key]["r"] = rgba[0];
    parent[key]["g"] = rgba[1];
    parent[key]["b"] = rgba[2];
    parent[key]["a"] = rgba[3];
}

inline void readColor(const Value& parent, const char* key, float rgba[4]) {
    if (!parent.has(key))
        return;
    const Value& c = parent[key];
    rgba[0] = c["r"].as_float(rgba[0]);
    rgba[1] = c["g"].as_float(rgba[1]);
    rgba[2] = c["b"].as_float(rgba[2]);
    rgba[3] = c["a"].as_float(rgba[3]);
}

class Parser {
public:
    static bool parse(const std::string& json, Value& out) {
        size_t pos = 0;
        // Defensive top-level catch: a malformed config (e.g. number that
        // std::stod can't parse, truncated string) should fall back to
        // defaults, never propagate to MainThread which has no handler.
        try {
            return parseValue(json, pos, out);
        } catch (...) {
            return false;
        }
    }

private:
    static void skipWhitespace(const std::string& json, size_t& pos) {
        while (pos < json.length() &&
               (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
            pos++;
        }
    }

    static bool parseValue(const std::string& json, size_t& pos, Value& out) {
        skipWhitespace(json, pos);
        if (pos >= json.length())
            return false;

        char c = json[pos];

        if (c == '{')
            return parseObject(json, pos, out);
        if (c == '"')
            return parseString(json, pos, out);
        if (c == 't' && json.substr(pos, 4) == "true") {
            pos += 4;
            out = Value(true);
            return true;
        }
        if (c == 'f' && json.substr(pos, 5) == "false") {
            pos += 5;
            out = Value(false);
            return true;
        }
        if (c == 'n' && json.substr(pos, 4) == "null") {
            pos += 4;
            out = Value();
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9'))
            return parseNumber(json, pos, out);
        if (c == '[')
            return parseArray(json, pos, out);

        return false;
    }

    static bool parseObject(const std::string& json, size_t& pos, Value& out) {
        if (pos >= json.length() || json[pos] != '{')
            return false;
        pos++;

        out.type_ = Value::Object;
        out.object_.clear();

        while (pos < json.length()) {
            skipWhitespace(json, pos);
            if (pos >= json.length())
                return false;
            if (json[pos] == '}') {
                pos++;
                return true;
            }

            Value keyVal;
            if (!parseString(json, pos, keyVal))
                return false;
            std::string key = keyVal.as_string();

            skipWhitespace(json, pos);
            if (pos >= json.length() || json[pos] != ':')
                return false;
            pos++;

            Value val;
            if (!parseValue(json, pos, val))
                return false;
            out.object_[key] = val;

            skipWhitespace(json, pos);
            if (pos >= json.length())
                return false;
            if (json[pos] == ',')
                pos++;
        }
        return false;
    }

    static bool parseArray(const std::string& json, size_t& pos, Value& out) {
        if (pos >= json.length() || json[pos] != '[')
            return false;
        pos++;
        out.type_ = Value::Array;

        while (pos < json.length()) {
            skipWhitespace(json, pos);
            if (pos >= json.length())
                return false;
            if (json[pos] == ']') {
                pos++;
                return true;
            }

            Value val;
            if (!parseValue(json, pos, val))
                return false;
            // Skip storing array values for now

            skipWhitespace(json, pos);
            if (pos >= json.length())
                return false;
            if (json[pos] == ',')
                pos++;
        }
        return false;
    }

    static bool parseString(const std::string& json, size_t& pos, Value& out) {
        skipWhitespace(json, pos);
        if (pos >= json.length() || json[pos] != '"')
            return false;
        pos++;

        std::string s;
        while (pos < json.length() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.length()) {
                pos++;
                switch (json[pos]) {
                    case '"':
                        s += '"';
                        break;
                    case '\\':
                        s += '\\';
                        break;
                    case 'n':
                        s += '\n';
                        break;
                    case 't':
                        s += '\t';
                        break;
                    case 'r':
                        s += '\r';
                        break;
                    default:
                        s += json[pos];
                        break;
                }
            } else {
                s += json[pos];
            }
            pos++;
        }

        if (pos >= json.length())
            return false;
        pos++;
        out = Value(s);
        return true;
    }

    static bool parseNumber(const std::string& json, size_t& pos, Value& out) {
        size_t start = pos;
        if (pos < json.length() && json[pos] == '-')
            pos++;
        while (pos < json.length() && json[pos] >= '0' && json[pos] <= '9')
            pos++;
        if (pos < json.length() && json[pos] == '.') {
            pos++;
            while (pos < json.length() && json[pos] >= '0' && json[pos] <= '9')
                pos++;
        }
        // Tolerate scientific notation (1e5, 2.3E-4) — consume but don't require.
        if (pos < json.length() && (json[pos] == 'e' || json[pos] == 'E')) {
            pos++;
            if (pos < json.length() && (json[pos] == '+' || json[pos] == '-'))
                pos++;
            while (pos < json.length() && json[pos] >= '0' && json[pos] <= '9')
                pos++;
        }

        if (pos == start)
            return false;

        std::string numStr = json.substr(start, pos - start);
        // Reject lone '-' / '-.' / '.' which std::stod would throw on.
        if (numStr == "-" || numStr == "." || numStr == "-.")
            return false;

        try {
            out = Value(std::stod(numStr));
        } catch (...) {
            return false;
        }
        return true;
    }
};

}  // namespace json
