#include "jpr/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace jpr {
namespace json {

const Value* Value::find(const std::string& key) const {
    if (type_ != Type::Object)
        return nullptr;
    for (auto const& entry : object_) {
        if (entry.first == key)
            return &entry.second;
    }
    return nullptr;
}

void Value::set(const std::string& key, Value value) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_.clear();
    }
    for (auto& entry : object_) {
        if (entry.first == key) {
            entry.second = std::move(value);
            return;
        }
    }
    object_.emplace_back(key, std::move(value));
}

namespace {

class Parser {
public:
    Parser(const std::string& text) : text_(text) {}

    bool parse(Value& out) {
        skip();
        if (!parseValue(out))
            return false;
        skip();
        if (pos_ != text_.size())
            return fail("trailing content after top level value");
        return true;
    }

    const std::string& error() const { return error_; }

private:
    const std::string& text_;
    size_t pos_ = 0;
    std::string error_;

    bool fail(const char* what) {
        if (error_.empty()) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s at offset %zu", what, pos_);
            error_ = buf;
        }
        return false;
    }

    bool eof() const { return pos_ >= text_.size(); }
    char peek() const { return text_[pos_]; }

    // Skips whitespace plus // line and /* block */ comments.
    void skip() {
        while (!eof()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                pos_++;
            } else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
                while (!eof() && text_[pos_] != '\n')
                    pos_++;
            } else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '*') {
                pos_ += 2;
                while (pos_ + 1 < text_.size() && !(text_[pos_] == '*' && text_[pos_ + 1] == '/'))
                    pos_++;
                pos_ = pos_ + 1 < text_.size() ? pos_ + 2 : text_.size();
            } else {
                return;
            }
        }
    }

    bool literal(const char* word) {
        size_t len = strlen(word);
        if (text_.compare(pos_, len, word) != 0)
            return false;
        pos_ += len;
        return true;
    }

    bool parseValue(Value& out) {
        if (eof())
            return fail("unexpected end of input");
        char c = peek();
        switch (c) {
        case '{': return parseObject(out);
        case '[': return parseArray(out);
        case '"': {
            std::string s;
            if (!parseString(s))
                return false;
            out = Value(std::move(s));
            return true;
        }
        case 't':
            if (!literal("true"))
                return fail("invalid literal");
            out = Value(true);
            return true;
        case 'f':
            if (!literal("false"))
                return fail("invalid literal");
            out = Value(false);
            return true;
        case 'n':
            if (!literal("null"))
                return fail("invalid literal");
            out = Value();
            return true;
        default: return parseNumber(out);
        }
    }

    bool parseObject(Value& out) {
        pos_++;  // '{'
        Object obj;
        skip();
        if (!eof() && peek() == '}') {
            pos_++;
            out = Value(std::move(obj));
            return true;
        }
        while (true) {
            skip();
            if (!eof() && peek() == '}') {  // trailing comma
                pos_++;
                break;
            }
            if (eof() || peek() != '"')
                return fail("expected object key");
            std::string key;
            if (!parseString(key))
                return false;
            skip();
            if (eof() || peek() != ':')
                return fail("expected ':'");
            pos_++;
            skip();
            Value value;
            if (!parseValue(value))
                return false;
            obj.emplace_back(std::move(key), std::move(value));
            skip();
            if (!eof() && peek() == ',') {
                pos_++;
                continue;
            }
            if (!eof() && peek() == '}') {
                pos_++;
                break;
            }
            return fail("expected ',' or '}'");
        }
        out = Value(std::move(obj));
        return true;
    }

    bool parseArray(Value& out) {
        pos_++;  // '['
        Array arr;
        skip();
        if (!eof() && peek() == ']') {
            pos_++;
            out = Value(std::move(arr));
            return true;
        }
        while (true) {
            skip();
            if (!eof() && peek() == ']') {  // trailing comma
                pos_++;
                break;
            }
            Value value;
            if (!parseValue(value))
                return false;
            arr.push_back(std::move(value));
            skip();
            if (!eof() && peek() == ',') {
                pos_++;
                continue;
            }
            if (!eof() && peek() == ']') {
                pos_++;
                break;
            }
            return fail("expected ',' or ']'");
        }
        out = Value(std::move(arr));
        return true;
    }

    bool parseString(std::string& out) {
        pos_++;  // opening quote
        out.clear();
        while (true) {
            if (eof())
                return fail("unterminated string");
            char c = text_[pos_++];
            if (c == '"')
                return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (eof())
                return fail("unterminated escape");
            char e = text_[pos_++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                if (pos_ + 4 > text_.size())
                    return fail("truncated \\u escape");
                unsigned code = 0;
                for (int i = 0; i < 4; i++) {
                    char h = text_[pos_++];
                    code <<= 4;
                    if (h >= '0' && h <= '9')
                        code |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f')
                        code |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F')
                        code |= (unsigned)(h - 'A' + 10);
                    else
                        return fail("invalid \\u escape");
                }
                appendUtf8(out, code);
                break;
            }
            default: return fail("invalid escape");
            }
        }
    }

    static void appendUtf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out.push_back((char)code);
        } else if (code < 0x800) {
            out.push_back((char)(0xC0 | (code >> 6)));
            out.push_back((char)(0x80 | (code & 0x3F)));
        } else {
            out.push_back((char)(0xE0 | (code >> 12)));
            out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (code & 0x3F)));
        }
    }

    bool parseNumber(Value& out) {
        size_t start = pos_;
        if (!eof() && (peek() == '-' || peek() == '+'))
            pos_++;
        bool digits = false;
        while (!eof() && peek() >= '0' && peek() <= '9') {
            pos_++;
            digits = true;
        }
        if (!eof() && peek() == '.') {
            pos_++;
            while (!eof() && peek() >= '0' && peek() <= '9') {
                pos_++;
                digits = true;
            }
        }
        if (!digits)
            return fail("expected a value");
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            pos_++;
            if (!eof() && (peek() == '-' || peek() == '+'))
                pos_++;
            while (!eof() && peek() >= '0' && peek() <= '9')
                pos_++;
        }
        out = Value(strtod(text_.substr(start, pos_ - start).c_str(), nullptr));
        return true;
    }
};

void dumpValue(const Value& value, int indent, int depth, std::string& out) {
    auto newline = [&](int d) {
        if (indent < 0)
            return;
        out.push_back('\n');
        out.append((size_t)(indent * d), ' ');
    };

    switch (value.type()) {
    case Value::Type::Null: out += "null"; break;
    case Value::Type::Bool: out += value.asBool() ? "true" : "false"; break;
    case Value::Type::Number: {
        double n = value.asNumber();
        char buf[40];
        if (std::isfinite(n) && n == (double)(int64_t)n && std::fabs(n) < 1e15)
            snprintf(buf, sizeof(buf), "%lld", (long long)n);
        else if (std::isfinite(n))
            snprintf(buf, sizeof(buf), "%.10g", n);
        else
            snprintf(buf, sizeof(buf), "0");
        out += buf;
        break;
    }
    case Value::Type::String: {
        out.push_back('"');
        for (char c : value.asString()) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
            }
        }
        out.push_back('"');
        break;
    }
    case Value::Type::Array: {
        if (value.array().empty()) {
            out += "[]";
            break;
        }
        // Short all-number arrays (ranges, mostly) stay on one line.
        bool inlineArray = value.array().size() <= 4;
        for (auto const& item : value.array()) {
            if (!item.isNumber() && !item.isBool())
                inlineArray = false;
        }
        out.push_back('[');
        for (size_t i = 0; i < value.array().size(); i++) {
            if (i)
                out += inlineArray ? ", " : ",";
            if (!inlineArray)
                newline(depth + 1);
            dumpValue(value.array()[i], indent, depth + 1, out);
        }
        if (!inlineArray)
            newline(depth);
        out.push_back(']');
        break;
    }
    case Value::Type::Object: {
        if (value.object().empty()) {
            out += "{}";
            break;
        }
        out.push_back('{');
        for (size_t i = 0; i < value.object().size(); i++) {
            if (i)
                out.push_back(',');
            newline(depth + 1);
            dumpValue(Value(value.object()[i].first), indent, depth + 1, out);
            out += indent < 0 ? ":" : ": ";
            dumpValue(value.object()[i].second, indent, depth + 1, out);
        }
        newline(depth);
        out.push_back('}');
        break;
    }
    }
}

}  // namespace

Value parse(const std::string& text, std::string* error) {
    Parser parser(text);
    Value out;
    if (!parser.parse(out)) {
        if (error)
            *error = parser.error();
        return Value();
    }
    if (error)
        error->clear();
    return out;
}

std::string dump(const Value& value, int indent) {
    std::string out;
    dumpValue(value, indent, 0, out);
    if (indent >= 0)
        out.push_back('\n');
    return out;
}

}  // namespace json
}  // namespace jpr
