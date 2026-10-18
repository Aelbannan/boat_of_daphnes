// CVars: see cvar.h. The design (typed values by dotted name, nested JSON under
// "CVars", the CVar bridge's names) follows libultraship's ConsoleVariable
// (MIT License, Copyright (c) 2022 kenix3); this file is a fresh implementation
// on picojson (BSD-2-Clause, vendored in the runtime's Externals).
// The Windows build force-includes a POSIX layer whose rename() macro breaks
// <filesystem>; this file uses std::filesystem::rename.
#ifdef rename
#undef rename
#endif

#include "cvar.h"

#define PICOJSON_USE_INT64
#include "picojson.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

namespace {

enum class Kind { Integer, Float, String, Color };

struct Value {
    Kind kind = Kind::Integer;
    int32_t integer = 0;
    float real = 0.f;
    std::string text;
    CVarColor color{};
};

std::recursive_mutex g_lock;
std::map<std::string, Value> g_values;
std::string g_path;
bool g_save_pending;

void load_object(const std::string& prefix, const picojson::object& object) {
    for (const auto& [key, node] : object) {
        const std::string name = prefix.empty() ? key : prefix + "." + key;
        Value v;
        if (node.is<picojson::object>()) {
            const auto& o = node.get<picojson::object>();
            // A colour is an object of R, G, B and A numbers.
            const auto channel = [&](const char* c, bool* found) -> uint8_t {
                const auto it = o.find(c);
                if (it == o.end() || !it->second.is<double>())
                    return 0;
                *found = true;
                return static_cast<uint8_t>(std::lround(it->second.get<double>()));
            };
            bool r = false, g = false, b = false, a = false;
            CVarColor color{channel("R", &r), channel("G", &g), channel("B", &b), channel("A", &a)};
            if (r && g && b && o.size() <= 4u) {
                v.kind = Kind::Color;
                v.color = color;
                if (!a)
                    v.color.a = 255;
                g_values[name] = v;
            } else {
                load_object(name, o);
            }
        } else if (node.is<int64_t>()) {
            v.kind = Kind::Integer;
            v.integer = static_cast<int32_t>(node.get<int64_t>());
            g_values[name] = v;
        } else if (node.is<double>()) {
            v.kind = Kind::Float;
            v.real = static_cast<float>(node.get<double>());
            g_values[name] = v;
        } else if (node.is<bool>()) {
            v.kind = Kind::Integer;
            v.integer = node.get<bool>() ? 1 : 0;
            g_values[name] = v;
        } else if (node.is<std::string>()) {
            v.kind = Kind::String;
            v.text = node.get<std::string>();
            g_values[name] = v;
        }
    }
}

picojson::value to_json(const Value& v) {
    switch (v.kind) {
    case Kind::Integer:
        return picojson::value(static_cast<int64_t>(v.integer));
    case Kind::Float:
        return picojson::value(static_cast<double>(v.real));
    case Kind::String:
        return picojson::value(v.text);
    case Kind::Color: {
        picojson::object o;
        o["R"] = picojson::value(static_cast<int64_t>(v.color.r));
        o["G"] = picojson::value(static_cast<int64_t>(v.color.g));
        o["B"] = picojson::value(static_cast<int64_t>(v.color.b));
        o["A"] = picojson::value(static_cast<int64_t>(v.color.a));
        return picojson::value(o);
    }
    }
    return picojson::value();
}

// The settings path is UTF-8 on every platform.
std::filesystem::path utf8_path(const std::string& text) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
#else
    return std::filesystem::u8path(text);
#endif
}

Value* find(const char* name) {
    if (name == nullptr)
        return nullptr;
    const auto it = g_values.find(name);
    return it == g_values.end() ? nullptr : &it->second;
}

void set(const char* name, Value v) {
    if (name == nullptr || name[0] == '\0')
        return;
    std::lock_guard lock(g_lock);
    g_values[name] = std::move(v);
}

} // namespace

extern "C" {

void CVarInit(const char* path) {
    std::lock_guard lock(g_lock);
    g_path = path != nullptr ? path : "";
    g_values.clear();
    std::ifstream in(utf8_path(g_path), std::ios::binary);
    if (!in)
        return;
    std::stringstream text;
    text << in.rdbuf();
    picojson::value root;
    const std::string error = picojson::parse(root, text.str());
    if (!error.empty() || !root.is<picojson::object>()) {
        // Kept beside it as .unreadable, since the next save replaces it.
        in.close();
        auto keep = utf8_path(g_path);
        keep += ".unreadable";
        std::error_code copy_error;
        std::filesystem::copy_file(utf8_path(g_path), keep, std::filesystem::copy_options::overwrite_existing,
                                   copy_error);
        std::fprintf(stderr, "[cvar] %s: unreadable (%s); starting from defaults%s\n", g_path.c_str(),
                     error.empty() ? "not an object" : error.c_str(), copy_error ? "" : ", a copy kept as .unreadable");
        return;
    }
    const auto& top = root.get<picojson::object>();
    const auto cvars = top.find("CVars");
    if (cvars != top.end() && cvars->second.is<picojson::object>())
        load_object("", cvars->second.get<picojson::object>());
    std::fprintf(stderr, "[cvar] %zu settings from %s\n", g_values.size(), g_path.c_str());
}

const char* CVarPath(void) { return g_path.c_str(); }

bool CVarExists(const char* name) {
    std::lock_guard lock(g_lock);
    return find(name) != nullptr;
}

int32_t CVarGetInteger(const char* name, int32_t defaultValue) {
    std::lock_guard lock(g_lock);
    const Value* v = find(name);
    if (v == nullptr)
        return defaultValue;
    if (v->kind == Kind::Integer)
        return v->integer;
    if (v->kind == Kind::Float)
        return static_cast<int32_t>(std::lround(v->real));
    return defaultValue;
}

float CVarGetFloat(const char* name, float defaultValue) {
    std::lock_guard lock(g_lock);
    const Value* v = find(name);
    if (v == nullptr)
        return defaultValue;
    if (v->kind == Kind::Float)
        return v->real;
    if (v->kind == Kind::Integer)
        return static_cast<float>(v->integer);
    return defaultValue;
}

const char* CVarGetString(const char* name, const char* defaultValue) {
    std::lock_guard lock(g_lock);
    const Value* v = find(name);
    return v != nullptr && v->kind == Kind::String ? v->text.c_str() : defaultValue;
}

CVarColor CVarGetColor(const char* name, CVarColor defaultValue) {
    std::lock_guard lock(g_lock);
    const Value* v = find(name);
    return v != nullptr && v->kind == Kind::Color ? v->color : defaultValue;
}

void CVarSetInteger(const char* name, int32_t value) {
    Value v;
    v.kind = Kind::Integer;
    v.integer = value;
    set(name, v);
}

void CVarSetFloat(const char* name, float value) {
    Value v;
    v.kind = Kind::Float;
    v.real = value;
    set(name, v);
}

void CVarSetString(const char* name, const char* value) {
    Value v;
    v.kind = Kind::String;
    v.text = value != nullptr ? value : "";
    set(name, v);
}

void CVarSetColor(const char* name, CVarColor value) {
    Value v;
    v.kind = Kind::Color;
    v.color = value;
    set(name, v);
}

void CVarClear(const char* name) {
    std::lock_guard lock(g_lock);
    if (name != nullptr)
        g_values.erase(name);
}

void CVarClearBlock(const char* prefix) {
    std::lock_guard lock(g_lock);
    if (prefix == nullptr)
        return;
    const size_t n = std::strlen(prefix);
    for (auto it = g_values.begin(); it != g_values.end();)
        it = it->first.compare(0, n, prefix) == 0 ? g_values.erase(it) : std::next(it);
}

void CVarSave(void) {
    std::lock_guard lock(g_lock);
    g_save_pending = false;
    if (g_path.empty())
        return;
    picojson::object cvars;
    for (const auto& [name, value] : g_values) {
        // Nest by the dots: gSettings.Graphics.Vsync -> CVars.gSettings.Graphics.Vsync.
        picojson::object* node = &cvars;
        size_t start = 0;
        for (;;) {
            const size_t dot = name.find('.', start);
            const std::string key = name.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            if (dot == std::string::npos) {
                (*node)[key] = to_json(value);
                break;
            }
            picojson::value& child = (*node)[key];
            if (!child.is<picojson::object>())
                child = picojson::value(picojson::object());
            node = &child.get<picojson::object>();
            start = dot + 1;
        }
    }
    picojson::object root;
    root["CVars"] = picojson::value(cvars);
    const std::string text = picojson::value(root).serialize(true);
    const auto path = utf8_path(g_path);
    auto pending = path;
    pending += ".pending";
    {
        std::ofstream out(pending, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::fprintf(stderr, "[cvar] cannot write %s\n", g_path.c_str());
            return;
        }
        out << text;
        if (!out.flush()) {
            std::fprintf(stderr, "[cvar] cannot write %s\n", g_path.c_str());
            return;
        }
    }
    std::error_code error;
    std::filesystem::rename(pending, path, error);
    if (error)
        std::fprintf(stderr, "[cvar] cannot replace %s: %s\n", g_path.c_str(), error.message().c_str());
}

void CVarSaveNextFrame(void) {
    std::lock_guard lock(g_lock);
    g_save_pending = true;
}

void CVarTick(void) {
    bool save;
    {
        std::lock_guard lock(g_lock);
        save = g_save_pending;
    }
    if (save)
        CVarSave();
}

} // extern "C"
