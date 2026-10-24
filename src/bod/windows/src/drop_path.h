// What a drop on the window carries, as a path the program can open (header only, so the test needs no SDL).
//
// SDL_EVENT_DROP_FILE brings a path. Under Proton/Wine, and from some Linux file managers, the window instead gets
// SDL_EVENT_DROP_TEXT: a text/uri-list ("file:///home/me/seed.json", percent-encoded, one per line, CR LF between them,
// "#" lines are comments), or a bare Unix path that Wine's file functions need as Z:\home\me\seed.json.
#ifndef BLUEWAKE_DROP_PATH_H
#define BLUEWAKE_DROP_PATH_H

#include <cctype>
#include <string>

namespace droppath {

inline int hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

inline std::string percent_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() && hex_value(in[i + 1]) >= 0 && hex_value(in[i + 2]) >= 0) {
            out += static_cast<char>(hex_value(in[i + 1]) * 16 + hex_value(in[i + 2]));
            i += 2;
        } else {
            out += in[i];
        }
    }
    return out;
}

inline bool starts_with_nocase(const std::string& s, const char* prefix) {
    for (size_t i = 0; prefix[i] != 0; ++i)
        if (i >= s.size() || std::tolower(static_cast<unsigned char>(s[i])) != prefix[i])
            return false;
    return true;
}

// The first entry of a drop's text, as a path: "" when there is none. `dos` (the program is a Windows one, which includes
// Wine): a Unix path ("/home/me/x") becomes Z:\home\me\x, where Wine has the whole Unix tree, and "file:///C:/x" loses its
// leading slash.
inline std::string to_path(const std::string& text, bool dos) {
    size_t begin = 0;
    std::string line;
    while (begin <= text.size()) {
        size_t end = text.find_first_of("\r\n", begin);
        if (end == std::string::npos)
            end = text.size();
        line = text.substr(begin, end - begin);
        begin = end + 1;
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') {
            line.clear();
            continue;
        }
        line = line.substr(first, line.find_last_not_of(" \t") - first + 1);
        break;
    }
    if (line.empty())
        return "";
    if (line.size() > 1 && line.front() == '"' && line.back() == '"')
        line = line.substr(1, line.size() - 2);
    if (starts_with_nocase(line, "file:")) {
        std::string rest = line.substr(5);
        if (rest.compare(0, 2, "//") == 0) {
            rest = rest.substr(2); // the authority: empty or localhost, then the path
            const size_t slash = rest.find('/');
            if (slash == std::string::npos)
                return "";
            rest = rest.substr(slash);
        }
        line = percent_decode(rest);
        if (line.size() >= 3 && line[0] == '/' && std::isalpha(static_cast<unsigned char>(line[1])) && line[2] == ':')
            line.erase(0, 1); // /C:/x
    }
    if (dos && line.size() >= 1 && line[0] == '/' && !(line.size() > 1 && line[1] == '/')) {
        std::string dos_path = "Z:";
        for (char c : line)
            dos_path += c == '/' ? '\\' : c;
        return dos_path;
    }
    return line;
}

} // namespace droppath

#endif
