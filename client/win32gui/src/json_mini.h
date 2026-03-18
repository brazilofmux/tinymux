// json_mini.h -- Minimal JSON reader/writer for config files.
// Supports: objects, arrays, strings, integers, bools. No nesting beyond
// array-of-objects. Enough for Titan's settings files.
#ifndef JSON_MINI_H
#define JSON_MINI_H

#include <string>
#include <vector>
#include <unordered_map>

// A JSON value: string, int, bool, or null.
struct JValue {
    enum Type { T_NULL, T_STRING, T_INT, T_BOOL } type = T_NULL;
    std::string sval;
    int64_t     ival = 0;
    bool        bval = false;

    JValue() {}
    JValue(const std::string& s) : type(T_STRING), sval(s) {}
    JValue(const char* s) : type(T_STRING), sval(s) {}
    JValue(int64_t i) : type(T_INT), ival(i) {}
    JValue(int i) : type(T_INT), ival(i) {}
    JValue(bool b) : type(T_BOOL), bval(b) {}

    std::string str() const { return sval; }
    int64_t     num() const { return ival; }
    bool        boolean() const { return bval; }
};

// A JSON object: ordered key-value map.
using JObject = std::vector<std::pair<std::string, JValue>>;

// A JSON array of objects (for worlds, triggers).
using JArray = std::vector<JObject>;

// Parse a JSON file into a top-level object.
// Arrays are returned as a special key with "_array:" prefix.
bool json_parse_file(const std::string& path, JObject& out);

// Parse a JSON file that is an array of objects.
bool json_parse_array_file(const std::string& path, JArray& out);

// Write a JSON object to a file.
bool json_write_file(const std::string& path, const JObject& obj);

// Write a JSON array of objects to a file.
bool json_write_array_file(const std::string& path, const JArray& arr);

// Helpers
const JValue* jget(const JObject& obj, const std::string& key);
std::string   jstr(const JObject& obj, const std::string& key, const std::string& def = "");
int64_t       jint(const JObject& obj, const std::string& key, int64_t def = 0);
bool          jbool(const JObject& obj, const std::string& key, bool def = false);

#endif // JSON_MINI_H
