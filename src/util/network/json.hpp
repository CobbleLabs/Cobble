#pragma once

#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Minimal JSON parser/serializer for config sync.
//
// IMPORTANT design note: the object backing store is a std::vector of
// key/value pairs, NOT a std::map. `Value` contains a container of `Value`,
// i.e. a recursive/incomplete type at the point of the member declaration.
// std::vector is explicitly allowed to hold an incomplete type (C++17
// [vector.overview]); std::map is NOT — doing so is undefined behavior. The
// previous implementation used std::map<std::string, Value> and hard-faulted
// (access violation) inside object construction/clear on some builds. Using a
// vector makes the recursive type well-defined. Object key order is preserved
// as insertion order, which also makes serialize() output stable.

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

    // ── Object access ──────────────────────────────────────────────────────
    // Mutable lookup: promotes to Object on first use and inserts a Null node
    // for a missing key (mirrors std::map::operator[] semantics that callers
    // relied on).
    Value& operator[](const std::string& key) {
        if (type_ != Object) {
            type_ = Object;
            members_.clear();
        }
        for (auto& kv : members_) {
            if (kv.first == key)
                return kv.second;
        }
        members_.emplace_back(key, Value());
        return members_.back().second;
    }

    const Value& operator[](const std::string& key) const {
        static const Value null;
        if (type_ != Object)
            return null;
        for (const auto& kv : members_) {
            if (kv.first == key)
                return kv.second;
        }
        return null;
    }

    bool has(const std::string& key) const {
        if (type_ != Object)
            return false;
        for (const auto& kv : members_) {
            if (kv.first == key)
                return true;
        }
        return false;
    }

    // Remove a key from an object node. No-op if absent or not an object.
    // Replacement for the old std::map-based `.object().erase(key)`.
    void remove(const std::string& key) {
        if (type_ != Object)
            return;
        for (auto it = members_.begin(); it != members_.end(); ++it) {
            if (it->first == key) {
                members_.erase(it);
                return;
            }
        }
    }

    // Compatibility shim for the old std::map-based API. Callers iterate with
    // range-for over `.object()` reading `kv.first` / `kv.second`, which works
    // identically on this pair vector. The const overload does NOT mutate type;
    // the non-const overload promotes to Object like the map version did.
    using Member = std::pair<std::string, Value>;
    using Members = std::vector<Member>;

    const Members& object() const { return members_; }
    Members& object() {
        type_ = Object;
        return members_;
    }

    // ── Array access ───────────────────────────────────────────────────────
    const std::vector<Value>& array() const { return array_; }
    std::vector<Value>& array() {
        type_ = Array;
        return array_;
    }
    void push_back(const Value& v) {
        if (type_ != Array) {
            type_ = Array;
            array_.clear();
        }
        array_.push_back(v);
    }
    size_t size() const {
        if (type_ == Array)
            return array_.size();
        if (type_ == Object)
            return members_.size();
        return 0;
    }

    // ── Serialization ──────────────────────────────────────────────────────
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
                if (number_ == static_cast<long long>(number_))
                    ss << static_cast<long long>(number_);
                else
                    // Significant-digit precision (not std::fixed) so floats like
                    // color components (0.0549) round-trip instead of being
                    // truncated to 2 decimals and drifting on every save/load.
                    // 9 digits is float max_digits10; no trailing-zero bloat.
                    ss << std::setprecision(9) << number_;
                break;
            case String:
                serializeString(ss, string_);
                break;
            case Object: {
                ss << '{';
                bool first = true;
                for (const auto& kv : members_) {
                    if (!first)
                        ss << ',';
                    first = false;
                    serializeString(ss, kv.first);
                    ss << ':';
                    kv.second.serialize(ss);
                }
                ss << '}';
                break;
            }
            case Array: {
                ss << '[';
                bool first = true;
                for (const auto& v : array_) {
                    if (!first)
                        ss << ',';
                    first = false;
                    v.serialize(ss);
                }
                ss << ']';
                break;
            }
        }
    }

private:
    static void serializeString(std::ostringstream& ss, const std::string& s) {
        ss << '"';
        for (char c : s) {
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
    }

    Type type_;
    bool bool_;
    double number_;
    std::string string_;
    Members members_;           // object key/value pairs (insertion order)
    std::vector<Value> array_;  // array elements

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
    // Diagnostic: last parse failure position + reason. Kept from the previous
    // implementation so config.hpp's error logging still compiles. Set on the
    // failure paths; single-threaded at config load, so plain statics are fine.
    static inline size_t s_errPos = 0;
    static inline const char* s_errWhy = "";
    static inline std::string s_errBadToken = "";
    static inline size_t s_maxPos = 0;
    static inline int s_depth = 0;
    static inline int s_maxDepth = 0;

    static bool parse(const std::string& json, Value& out) {
        size_t pos = 0;
        s_errPos = 0;
        s_errWhy = "";
        s_errBadToken.clear();
        s_maxPos = 0;
        s_depth = 0;
        s_maxDepth = 0;
        try {
            if (!parseValue(json, pos, out)) {
                if (s_errWhy[0] == '\0')
                    s_errWhy = "parseValue returned false (top level)";
                return false;
            }
            skipWhitespace(json, pos);
            // Trailing non-whitespace after the root value is tolerated (some
            // callers embed config in a larger buffer), so we don't fail here.
            return true;
        } catch (const std::exception& e) {
            s_errWhy = "C++ exception in parse";
            s_errBadToken = e.what();
            return false;
        } catch (...) {
            s_errWhy = "unknown C++ exception in parse";
            return false;
        }
    }

private:
    // Depth guard: bound recursion so a pathologically nested (or malformed)
    // input can't blow the stack. Real configs nest ~6 deep.
    static constexpr int MAX_DEPTH = 64;

    static void skipWhitespace(const std::string& json, size_t& pos) {
        while (pos < json.length() &&
               (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
            pos++;
        }
    }

    static bool parseValue(const std::string& json, size_t& pos, Value& out) {
        if (pos > s_maxPos)
            s_maxPos = pos;
        skipWhitespace(json, pos);
        if (pos >= json.length()) {
            s_errPos = pos;
            s_errWhy = "parseValue: end of input";
            return false;
        }

        char c = json[pos];

        if (c == '{')
            return parseObject(json, pos, out);
        if (c == '[')
            return parseArray(json, pos, out);
        if (c == '"') {
            std::string s;
            if (!parseString(json, pos, s))
                return false;
            out = Value(s);
            return true;
        }
        if (c == 't' && json.compare(pos, 4, "true") == 0) {
            pos += 4;
            out = Value(true);
            return true;
        }
        if (c == 'f' && json.compare(pos, 5, "false") == 0) {
            pos += 5;
            out = Value(false);
            return true;
        }
        if (c == 'n' && json.compare(pos, 4, "null") == 0) {
            pos += 4;
            out = Value();
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9'))
            return parseNumber(json, pos, out);

        s_errPos = pos;
        s_errWhy = "parseValue: unexpected char";
        s_errBadToken = json.substr(pos, 16);
        return false;
    }

    static bool parseObject(const std::string& json, size_t& pos, Value& out) {
        pos++;  // consume '{'

        if (++s_depth > MAX_DEPTH) {
            s_errPos = pos;
            s_errWhy = "parseObject: max depth exceeded";
            s_depth--;
            return false;
        }
        if (s_depth > s_maxDepth)
            s_maxDepth = s_depth;

        out.type_ = Value::Object;
        out.members_.clear();

        skipWhitespace(json, pos);
        // Empty object.
        if (pos < json.length() && json[pos] == '}') {
            pos++;
            s_depth--;
            return true;
        }

        while (pos < json.length()) {
            if (pos > s_maxPos)
                s_maxPos = pos;

            skipWhitespace(json, pos);
            std::string key;
            if (!parseString(json, pos, key)) {
                s_errWhy = "parseObject: bad key";
                s_depth--;
                return false;
            }

            skipWhitespace(json, pos);
            if (pos >= json.length() || json[pos] != ':') {
                s_errPos = pos;
                s_errWhy = "parseObject: expected ':'";
                s_depth--;
                return false;
            }
            pos++;  // consume ':'

            // Append the member first, then parse into it in place — avoids
            // constructing a temporary Value and copying (which, with the old
            // recursive std::map, was where things went wrong).
            out.members_.emplace_back(key, Value());
            if (!parseValue(json, pos, out.members_.back().second)) {
                s_depth--;
                return false;
            }

            skipWhitespace(json, pos);
            if (pos >= json.length()) {
                s_errPos = pos;
                s_errWhy = "parseObject: unterminated";
                s_depth--;
                return false;
            }
            if (json[pos] == ',') {
                pos++;
                continue;
            }
            if (json[pos] == '}') {
                pos++;
                s_depth--;
                return true;
            }
            s_errPos = pos;
            s_errWhy = "parseObject: expected ',' or '}'";
            s_depth--;
            return false;
        }

        s_errPos = pos;
        s_errWhy = "parseObject: unterminated";
        s_depth--;
        return false;
    }

    static bool parseArray(const std::string& json, size_t& pos, Value& out) {
        pos++;  // consume '['

        if (++s_depth > MAX_DEPTH) {
            s_errPos = pos;
            s_errWhy = "parseArray: max depth exceeded";
            s_depth--;
            return false;
        }
        if (s_depth > s_maxDepth)
            s_maxDepth = s_depth;

        out.type_ = Value::Array;
        out.array_.clear();

        skipWhitespace(json, pos);
        // Empty array.
        if (pos < json.length() && json[pos] == ']') {
            pos++;
            s_depth--;
            return true;
        }

        while (pos < json.length()) {
            if (pos > s_maxPos)
                s_maxPos = pos;

            out.array_.emplace_back();
            if (!parseValue(json, pos, out.array_.back())) {
                s_depth--;
                return false;
            }

            skipWhitespace(json, pos);
            if (pos >= json.length()) {
                s_errPos = pos;
                s_errWhy = "parseArray: unterminated";
                s_depth--;
                return false;
            }
            if (json[pos] == ',') {
                pos++;
                continue;
            }
            if (json[pos] == ']') {
                pos++;
                s_depth--;
                return true;
            }
            s_errPos = pos;
            s_errWhy = "parseArray: expected ',' or ']'";
            s_depth--;
            return false;
        }

        s_errPos = pos;
        s_errWhy = "parseArray: unterminated";
        s_depth--;
        return false;
    }

    // Parse a JSON string literal into `out` (raw std::string, no Value wrap).
    static bool parseString(const std::string& json, size_t& pos, std::string& out) {
        skipWhitespace(json, pos);
        if (pos >= json.length() || json[pos] != '"') {
            s_errPos = pos;
            s_errWhy = "parseString: expected '\"'";
            return false;
        }
        pos++;  // consume opening quote

        out.clear();
        while (pos < json.length()) {
            char c = json[pos];
            if (c == '"') {
                pos++;  // consume closing quote
                return true;
            }
            if (c == '\\') {
                if (pos + 1 >= json.length()) {
                    s_errPos = pos;
                    s_errWhy = "parseString: trailing escape";
                    return false;
                }
                pos++;
                char e = json[pos];
                switch (e) {
                    case '"':
                        out += '"';
                        break;
                    case '\\':
                        out += '\\';
                        break;
                    case '/':
                        out += '/';
                        break;
                    case 'n':
                        out += '\n';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 'b':
                        out += '\b';
                        break;
                    case 'f':
                        out += '\f';
                        break;
                    case 'u': {
                        // Minimal \uXXXX handling: emit the low byte (config
                        // strings are ASCII in practice). Skip the 4 hex digits.
                        if (pos + 4 < json.length()) {
                            auto hex = [](char h) -> int {
                                if (h >= '0' && h <= '9')
                                    return h - '0';
                                if (h >= 'a' && h <= 'f')
                                    return h - 'a' + 10;
                                if (h >= 'A' && h <= 'F')
                                    return h - 'A' + 10;
                                return 0;
                            };
                            int cp = (hex(json[pos + 1]) << 12) | (hex(json[pos + 2]) << 8) |
                                     (hex(json[pos + 3]) << 4) | hex(json[pos + 4]);
                            pos += 4;
                            if (cp < 0x80) {
                                out += static_cast<char>(cp);
                            } else if (cp < 0x800) {
                                out += static_cast<char>(0xC0 | (cp >> 6));
                                out += static_cast<char>(0x80 | (cp & 0x3F));
                            } else {
                                out += static_cast<char>(0xE0 | (cp >> 12));
                                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                                out += static_cast<char>(0x80 | (cp & 0x3F));
                            }
                        }
                        break;
                    }
                    default:
                        out += e;
                        break;
                }
                pos++;
            } else {
                out += c;
                pos++;
            }
        }

        s_errPos = pos;
        s_errWhy = "parseString: unterminated";
        return false;
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
        if (pos < json.length() && (json[pos] == 'e' || json[pos] == 'E')) {
            pos++;
            if (pos < json.length() && (json[pos] == '+' || json[pos] == '-'))
                pos++;
            while (pos < json.length() && json[pos] >= '0' && json[pos] <= '9')
                pos++;
        }

        if (pos == start) {
            s_errPos = start;
            s_errWhy = "parseNumber: empty";
            return false;
        }

        std::string numStr = json.substr(start, pos - start);
        if (numStr == "-" || numStr == "." || numStr == "-.") {
            s_errPos = start;
            s_errWhy = "parseNumber: lone sign/dot";
            s_errBadToken = numStr;
            return false;
        }

        // Locale-independent parse: always treat '.' as the decimal separator.
        // std::stod honors the global C locale (the JVM sets it to the system
        // locale, which on ES/EU uses ',' ), so we parse by hand.
        out = Value(parseDoubleC(numStr.c_str()));
        return true;
    }

    // strtod replacement that ALWAYS uses '.' as the decimal separator,
    // independent of the process C locale. No allocation, no locale.
    static double parseDoubleC(const char* s) {
        const char* p = s;
        bool neg = false;
        if (*p == '+' || *p == '-') {
            neg = (*p == '-');
            p++;
        }
        double intPart = 0.0;
        while (*p >= '0' && *p <= '9') {
            intPart = intPart * 10.0 + (*p - '0');
            p++;
        }
        double frac = 0.0, scale = 1.0;
        if (*p == '.') {
            p++;
            while (*p >= '0' && *p <= '9') {
                frac = frac * 10.0 + (*p - '0');
                scale *= 10.0;
                p++;
            }
        }
        double val = intPart + (scale > 1.0 ? frac / scale : 0.0);
        if (*p == 'e' || *p == 'E') {
            p++;
            bool eneg = false;
            if (*p == '+' || *p == '-') {
                eneg = (*p == '-');
                p++;
            }
            int exp = 0;
            while (*p >= '0' && *p <= '9') {
                exp = exp * 10 + (*p - '0');
                p++;
            }
            double factor = 1.0;
            for (int i = 0; i < exp; i++)
                factor *= 10.0;
            val = eneg ? (val / factor) : (val * factor);
        }
        return neg ? -val : val;
    }
};

}  // namespace json
