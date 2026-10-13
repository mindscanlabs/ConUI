#pragma once
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// A minimal JSON parser and writer (docs/DESIGN_SYSTEM.md §15).
//
// Deliberately a STRICT SUBSET, not a general-purpose library: objects,
// arrays, strings, numbers, true/false/null, nesting a few levels deep. That
// is everything a theme file needs, and every line not written is a line that
// cannot be wrong.
//
// Hand-rolled parsers are a well-known source of quiet bugs, so the contract
// here is the opposite of quiet: any deviation from the grammar produces an
// error with a LINE NUMBER, and never a half-parsed document. A caller that
// gets `false` back has a message it can show a human.
// ---------------------------------------------------------------------------

namespace json {

struct Value;

/**
 * Members in source order. A vector rather than a map on purpose: themes are
 * written back out, and a file that reorders its own keys on every save is a
 * file nobody can diff.
 */
using Members = std::vector<std::pair<std::string, Value>>;

struct Value {
    enum class Type { Null, Bool, Number, String, Object, Array };

    Type        type = Type::Null;
    bool        boolean = false;
    double      number = 0.0;
    std::string string;
    Members     members;
    std::vector<Value> elements;

    bool isNull()   const { return type == Type::Null; }
    bool isObject() const { return type == Type::Object; }
    bool isString() const { return type == Type::String; }
    bool isNumber() const { return type == Type::Number; }

    /** Member by key, or null if absent or not an object. */
    const Value* get(const char* key) const;

    /** Convenience readers that leave `out` untouched when the type is wrong. */
    bool getString(const char* key, std::string& out) const;
    bool getInt(const char* key, int& out) const;
    bool getNumber(const char* key, double& out) const;
    bool getBool(const char* key, bool& out) const;
};

struct Error {
    int         line = 0;     // 1-based; 0 means "no error"
    std::string message;      // human-readable, no trailing period
    std::string where() const;  // "line 12: unterminated string"
};

/** Parses a whole document. On failure `out` is left untouched. */
bool Parse(const std::string& src, Value& out, Error& err);

/** Serialises back to text. Stable ordering, two-space indent, trailing \n. */
std::string Write(const Value& v, int indent = 2);

/** Builders, so callers do not have to fill the struct by hand. */
Value Str(std::string s);
Value Num(double n);
Value Obj(Members m);
Value Bool(bool b);
Value Arr(std::vector<Value> v);

} // namespace json
