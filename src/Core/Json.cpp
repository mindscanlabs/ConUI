#include "Json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace json {

namespace {

// Nesting is capped rather than left to the stack. A theme file is two levels
// deep; anything claiming to be forty is either a mistake or an attack, and
// either way the answer is an error with a line number, not a crash.
constexpr int kMaxDepth = 8;

struct Parser {
    const char* p = nullptr;
    const char* end = nullptr;
    int line = 1;
    Error* err = nullptr;

    bool fail(const char* msg) {
        if (err && err->line == 0) { err->line = line; err->message = msg; }
        return false;
    }

    bool eof() const { return p >= end; }
    char peek() const { return p < end ? *p : '\0'; }

    void advance() {
        if (p < end) {
            if (*p == '\n') ++line;
            ++p;
        }
    }

    void skipWs() {
        while (p < end) {
            const unsigned char c = (unsigned char)*p;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
            // Comments are not JSON. Saying so beats silently treating the
            // rest of the line as garbage.
            if (c == '/') { fail("comments are not supported"); return; }
            break;
        }
    }

    bool literal(const char* word) {
        const size_t n = std::strlen(word);
        if ((size_t)(end - p) < n || std::memcmp(p, word, n) != 0)
            return fail("unknown literal");
        for (size_t i = 0; i < n; ++i) advance();
        return true;
    }

    bool hex4(unsigned& out) {
        out = 0;
        for (int i = 0; i < 4; ++i) {
            if (eof()) return fail("truncated \\u escape");
            const char c = peek();
            unsigned d;
            if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
            else return fail("bad hex digit in \\u escape");
            out = out * 16 + d;
            advance();
        }
        return true;
    }

    void appendUtf8(std::string& s, unsigned cp) {
        if (cp < 0x80) {
            s += (char)cp;
        } else if (cp < 0x800) {
            s += (char)(0xC0 | (cp >> 6));
            s += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += (char)(0xE0 | (cp >> 12));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        } else {
            s += (char)(0xF0 | (cp >> 18));
            s += (char)(0x80 | ((cp >> 12) & 0x3F));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        }
    }

    bool parseString(std::string& out) {
        if (peek() != '"') return fail("expected a string");
        advance();
        out.clear();
        while (true) {
            if (eof()) return fail("unterminated string");
            const unsigned char c = (unsigned char)peek();
            if (c == '"') { advance(); return true; }
            if (c == '\n') return fail("unterminated string");
            if (c < 0x20) return fail("raw control character in string");
            if (c != '\\') { out += (char)c; advance(); continue; }

            advance();                                  // the backslash
            if (eof()) return fail("unterminated escape");
            const char e = peek();
            advance();
            switch (e) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp)) return false;
                // Surrogate pairs, because a file written by anything else may
                // well contain them.
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (end - p >= 2 && p[0] == '\\' && p[1] == 'u') {
                        advance(); advance();
                        unsigned lo = 0;
                        if (!hex4(lo)) return false;
                        if (lo < 0xDC00 || lo > 0xDFFF) return fail("bad low surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else {
                        return fail("lone high surrogate");
                    }
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return fail("lone low surrogate");
                }
                appendUtf8(out, cp);
                break;
            }
            default:
                return fail("unknown escape");
            }
        }
    }

    bool parseNumber(double& out) {
        const char* start = p;
        if (peek() == '-') advance();
        if (!std::isdigit((unsigned char)peek())) return fail("expected a digit");
        // JSON forbids leading zeros: 0 and 0.5 are numbers, 01 is not. Without
        // this the integer part is read as far as it goes and "01" quietly
        // becomes 1 — a file that is wrong but loads.
        if (peek() == '0') {
            advance();
            if (std::isdigit((unsigned char)peek())) return fail("leading zero in number");
        } else {
            while (std::isdigit((unsigned char)peek())) advance();
        }
        if (peek() == '.') {
            advance();
            if (!std::isdigit((unsigned char)peek())) return fail("expected a digit after '.'");
            while (std::isdigit((unsigned char)peek())) advance();
        }
        if (peek() == 'e' || peek() == 'E') {
            advance();
            if (peek() == '+' || peek() == '-') advance();
            if (!std::isdigit((unsigned char)peek())) return fail("expected a digit in exponent");
            while (std::isdigit((unsigned char)peek())) advance();
        }
        const std::string text(start, (size_t)(p - start));
        out = std::strtod(text.c_str(), nullptr);
        if (!std::isfinite(out)) return fail("number out of range");
        return true;
    }

    bool parseValue(Value& out, int depth) {
        if (depth > kMaxDepth) return fail("nesting too deep");
        skipWs();
        if (err && err->line) return false;
        if (eof()) return fail("unexpected end of input");

        switch (peek()) {
        case '{': return parseObject(out, depth);
        case '[': return parseArray(out, depth);
        case '"':
            out.type = Value::Type::String;
            return parseString(out.string);
        case 't':
            if (!literal("true")) return false;
            out.type = Value::Type::Bool; out.boolean = true;
            return true;
        case 'f':
            if (!literal("false")) return false;
            out.type = Value::Type::Bool; out.boolean = false;
            return true;
        case 'n':
            if (!literal("null")) return false;
            out.type = Value::Type::Null;
            return true;
        default:
            out.type = Value::Type::Number;
            return parseNumber(out.number);
        }
    }

    bool parseObject(Value& out, int depth) {
        advance();                                   // '{'
        out.type = Value::Type::Object;
        out.members.clear();
        skipWs();
        if (err && err->line) return false;
        if (peek() == '}') { advance(); return true; }

        while (true) {
            skipWs();
            if (err && err->line) return false;
            std::string key;
            if (!parseString(key)) return false;

            // Duplicate keys are an error, not a last-one-wins race: a theme
            // that sets bg.panel twice is a file someone edited by hand and
            // got wrong, and they need to be told which line.
            for (const auto& m : out.members)
                if (m.first == key) return fail("duplicate key");

            skipWs();
            if (peek() != ':') return fail("expected ':'");
            advance();

            Value v;
            if (!parseValue(v, depth + 1)) return false;
            out.members.emplace_back(std::move(key), std::move(v));

            skipWs();
            if (peek() == ',') { advance(); continue; }
            if (peek() == '}') { advance(); return true; }
            return fail(eof() ? "unterminated object" : "expected ',' or '}'");
        }
    }

    bool parseArray(Value& out, int depth) {
        advance();                                   // '['
        out.type = Value::Type::Array;
        out.elements.clear();
        skipWs();
        if (err && err->line) return false;
        if (peek() == ']') { advance(); return true; }

        while (true) {
            Value v;
            if (!parseValue(v, depth + 1)) return false;
            out.elements.push_back(std::move(v));
            skipWs();
            if (peek() == ',') { advance(); continue; }
            if (peek() == ']') { advance(); return true; }
            return fail(eof() ? "unterminated array" : "expected ',' or ']'");
        }
    }
};

void writeString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04X", c);
                out += buf;
            } else {
                out += (char)c;   // UTF-8 passes through unescaped
            }
        }
    }
    out += '"';
}

void writeNumber(std::string& out, double n) {
    // Integers must come back as integers: "version": 1, not 1.0.
    if (n == (double)(long long)n && std::fabs(n) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%lld", (long long)n);
        out += buf;
    } else {
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.17g", n);
        out += buf;
    }
}

void writeValue(std::string& out, const Value& v, int indent, int depth) {
    const std::string pad((size_t)((depth + 1) * indent), ' ');
    const std::string padEnd((size_t)(depth * indent), ' ');
    switch (v.type) {
    case Value::Type::Null:   out += "null"; break;
    case Value::Type::Bool:   out += v.boolean ? "true" : "false"; break;
    case Value::Type::Number: writeNumber(out, v.number); break;
    case Value::Type::String: writeString(out, v.string); break;
    case Value::Type::Object:
        if (v.members.empty()) { out += "{}"; break; }
        out += "{\n";
        for (size_t i = 0; i < v.members.size(); ++i) {
            out += pad;
            writeString(out, v.members[i].first);
            out += ": ";
            writeValue(out, v.members[i].second, indent, depth + 1);
            if (i + 1 < v.members.size()) out += ',';
            out += '\n';
        }
        out += padEnd;
        out += '}';
        break;
    case Value::Type::Array:
        if (v.elements.empty()) { out += "[]"; break; }
        out += "[\n";
        for (size_t i = 0; i < v.elements.size(); ++i) {
            out += pad;
            writeValue(out, v.elements[i], indent, depth + 1);
            if (i + 1 < v.elements.size()) out += ',';
            out += '\n';
        }
        out += padEnd;
        out += ']';
        break;
    }
}

} // namespace

const Value* Value::get(const char* key) const {
    if (type != Type::Object || !key) return nullptr;
    for (const auto& m : members)
        if (m.first == key) return &m.second;
    return nullptr;
}

bool Value::getString(const char* key, std::string& out) const {
    const Value* v = get(key);
    if (!v || !v->isString()) return false;
    out = v->string;
    return true;
}

bool Value::getInt(const char* key, int& out) const {
    const Value* v = get(key);
    if (!v || !v->isNumber()) return false;
    // 1e300 is valid JSON; casting it to int is undefined behaviour. Out of
    // range counts as "not an int", like a wrong type.
    if (!(v->number >= -2147483648.0 && v->number <= 2147483647.0)) return false;
    out = (int)v->number;
    return true;
}

bool Value::getNumber(const char* key, double& out) const {
    const Value* v = get(key);
    if (!v || !v->isNumber()) return false;
    out = v->number;
    return true;
}

bool Value::getBool(const char* key, bool& out) const {
    const Value* v = get(key);
    if (!v || v->type != Type::Bool) return false;
    out = v->boolean;
    return true;
}

std::string Error::where() const {
    if (!line) return {};
    char buf[32];
    std::snprintf(buf, sizeof buf, "line %d: ", line);
    return std::string(buf) + message;
}

bool Parse(const std::string& src, Value& out, Error& err) {
    err = Error{};

    // A UTF-8 BOM is common from Windows editors and is not an error.
    size_t begin = 0;
    if (src.size() >= 3 && (unsigned char)src[0] == 0xEF &&
        (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF)
        begin = 3;

    Parser ps;
    ps.p = src.data() + begin;
    ps.end = src.data() + src.size();
    ps.err = &err;

    Value tmp;
    if (!ps.parseValue(tmp, 0)) return false;
    ps.skipWs();
    if (err.line) return false;
    if (!ps.eof()) { ps.fail("trailing characters after the document"); return false; }

    out = std::move(tmp);
    return true;
}

std::string Write(const Value& v, int indent) {
    std::string out;
    writeValue(out, v, indent, 0);
    out += '\n';
    return out;
}

Value Str(std::string s) {
    Value v; v.type = Value::Type::String; v.string = std::move(s); return v;
}
Value Num(double n) {
    Value v; v.type = Value::Type::Number; v.number = n; return v;
}
Value Bool(bool b) {
    Value v; v.type = Value::Type::Bool; v.boolean = b; return v;
}

Value Arr(std::vector<Value> e) {
    Value v; v.type = Value::Type::Array; v.elements = std::move(e); return v;
}

Value Obj(Members m) {
    Value v; v.type = Value::Type::Object; v.members = std::move(m); return v;
}

} // namespace json
