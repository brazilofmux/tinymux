// json_mini.cpp -- Minimal JSON reader/writer.
#include "json_mini.h"
#include <fstream>
#include <sstream>
#include <cctype>

// -- Helpers --

const JValue* jget(const JObject& obj, const std::string& key) {
    for (auto& [k, v] : obj) if (k == key) return &v;
    return nullptr;
}

std::string jstr(const JObject& obj, const std::string& key, const std::string& def) {
    auto* v = jget(obj, key);
    return (v && v->type == JValue::T_STRING) ? v->sval : def;
}

int64_t jint(const JObject& obj, const std::string& key, int64_t def) {
    auto* v = jget(obj, key);
    return (v && v->type == JValue::T_INT) ? v->ival : def;
}

bool jbool(const JObject& obj, const std::string& key, bool def) {
    auto* v = jget(obj, key);
    return (v && v->type == JValue::T_BOOL) ? v->bval : def;
}

// -- Parser --

struct Parser {
    const char* p;
    const char* end;

    void skip_ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }

    bool expect(char c) {
        skip_ws();
        if (p < end && *p == c) { p++; return true; }
        return false;
    }

    bool parse_string(std::string& out) {
        skip_ws();
        if (p >= end || *p != '"') return false;
        p++;
        out.clear();
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                p++;
                switch (*p) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                default: out += *p; break;
                }
            } else {
                out += *p;
            }
            p++;
        }
        if (p < end) p++;  // skip closing quote
        return true;
    }

    bool parse_value(JValue& out) {
        skip_ws();
        if (p >= end) return false;

        if (*p == '"') {
            std::string s;
            if (!parse_string(s)) return false;
            out = JValue(s);
            return true;
        }
        if (*p == 't' && p + 4 <= end && strncmp(p, "true", 4) == 0) {
            out = JValue(true); p += 4; return true;
        }
        if (*p == 'f' && p + 5 <= end && strncmp(p, "false", 5) == 0) {
            out = JValue(false); p += 5; return true;
        }
        if (*p == 'n' && p + 4 <= end && strncmp(p, "null", 4) == 0) {
            out = JValue(); p += 4; return true;
        }
        if (*p == '-' || isdigit((unsigned char)*p)) {
            const char* start = p;
            if (*p == '-') p++;
            while (p < end && isdigit((unsigned char)*p)) p++;
            std::string num(start, p);
            out = JValue((int64_t)strtoll(num.c_str(), nullptr, 10));
            return true;
        }
        return false;
    }

    bool parse_object(JObject& out) {
        if (!expect('{')) return false;
        skip_ws();
        if (p < end && *p == '}') { p++; return true; }

        for (;;) {
            std::string key;
            if (!parse_string(key)) return false;
            if (!expect(':')) return false;
            JValue val;
            if (!parse_value(val)) return false;
            out.push_back({key, val});
            skip_ws();
            if (p < end && *p == ',') { p++; continue; }
            break;
        }
        return expect('}');
    }

    bool parse_array(JArray& out) {
        if (!expect('[')) return false;
        skip_ws();
        if (p < end && *p == ']') { p++; return true; }

        for (;;) {
            JObject obj;
            if (!parse_object(obj)) return false;
            out.push_back(std::move(obj));
            skip_ws();
            if (p < end && *p == ',') { p++; continue; }
            break;
        }
        return expect(']');
    }
};

bool json_parse_file(const std::string& path, JObject& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Parser parser = { data.c_str(), data.c_str() + data.size() };
    return parser.parse_object(out);
}

bool json_parse_array_file(const std::string& path, JArray& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Parser parser = { data.c_str(), data.c_str() + data.size() };
    return parser.parse_array(out);
}

// -- Writer --

static std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    out += '"';
    return out;
}

static std::string value_to_json(const JValue& v) {
    switch (v.type) {
    case JValue::T_STRING: return escape_json(v.sval);
    case JValue::T_INT:    return std::to_string(v.ival);
    case JValue::T_BOOL:   return v.bval ? "true" : "false";
    default:               return "null";
    }
}

static std::string object_to_json(const JObject& obj, int indent = 0) {
    std::string pad(indent, ' ');
    std::string pad2(indent + 2, ' ');
    std::string out = "{\n";
    for (size_t i = 0; i < obj.size(); i++) {
        out += pad2 + escape_json(obj[i].first) + ": " + value_to_json(obj[i].second);
        if (i + 1 < obj.size()) out += ",";
        out += "\n";
    }
    out += pad + "}";
    return out;
}

bool json_write_file(const std::string& path, const JObject& obj) {
    std::ofstream f(path);
    if (!f) return false;
    f << object_to_json(obj) << "\n";
    return true;
}

bool json_write_array_file(const std::string& path, const JArray& arr) {
    std::ofstream f(path);
    if (!f) return false;
    f << "[\n";
    for (size_t i = 0; i < arr.size(); i++) {
        f << "  " << object_to_json(arr[i], 2);
        if (i + 1 < arr.size()) f << ",";
        f << "\n";
    }
    f << "]\n";
    return true;
}
