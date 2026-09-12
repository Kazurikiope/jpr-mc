// Minimal, dependency-free JSON used for mod configuration.
//
// Supports the subset a config file needs: objects (insertion ordered),
// arrays, numbers, strings, booleans and null. The parser additionally
// tolerates // and /* */ comments and trailing commas so hand edited config
// files do not blow up in the user's face.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace jpr {
namespace json {

class Value;

using Object = std::vector<std::pair<std::string, Value>>;
using Array = std::vector<Value>;

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(bool v) : type_(Type::Bool), bool_(v) {}
    Value(double v) : type_(Type::Number), number_(v) {}
    Value(int v) : type_(Type::Number), number_((double)v) {}
    Value(int64_t v) : type_(Type::Number), number_((double)v) {}
    Value(const char* v) : type_(Type::String), string_(v) {}
    Value(std::string v) : type_(Type::String), string_(std::move(v)) {}
    Value(Array v) : type_(Type::Array), array_(std::move(v)) {}
    Value(Object v) : type_(Type::Object), object_(std::move(v)) {}

    static Value makeObject() { return Value(Object{}); }
    static Value makeArray() { return Value(Array{}); }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool def = false) const { return type_ == Type::Bool ? bool_ : def; }
    double asNumber(double def = 0.0) const { return type_ == Type::Number ? number_ : def; }
    std::string asString(const std::string& def = std::string()) const {
        return type_ == Type::String ? string_ : def;
    }

    const Array& array() const { return array_; }
    Array& array() { return array_; }
    const Object& object() const { return object_; }
    Object& object() { return object_; }

    // Object lookup. Returns nullptr when absent or when this is not an object.
    const Value* find(const std::string& key) const;

    // Object insert-or-replace, preserving insertion order for new keys.
    void set(const std::string& key, Value value);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    Array array_;
    Object object_;
};

// Parses `text`. On failure returns a Null value and fills `error` when given.
Value parse(const std::string& text, std::string* error = nullptr);

// Serialises with two-space indentation (indent < 0 emits compact output).
std::string dump(const Value& value, int indent = 2);

}  // namespace json
}  // namespace jpr
