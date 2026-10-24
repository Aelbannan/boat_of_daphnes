// See gb_browser.h. GameBanana's API (apiv11, no key): Mod/Index lists a category's mods a page at a time (name, author,
// likes, views, screenshots at 100/220/530/800 pixels wide), Mod/<id>/ProfilePage gives one's description, licence,
// downloads and files (each with its download link, size, MD5 and GameBanana's virus check). Requests run on threads of
// their own; the menu draws what has arrived. Pictures are JPEG or PNG (stb_image); a WebP one is skipped.
#ifdef rename
#undef rename  // bw_posix_compat.h's, which <filesystem> must not see
#endif

#include "gb_browser.h"

#include "mods.h"
#include "platform/bod_platform.h"
#include "ship/cvar.h"
#include "ship_menu.h"

#define PICOJSON_USE_INT64  // (as anchor.cpp: the parser the program links keeps whole numbers as int64)
#include "picojson.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../../third_party/stb/stb_image.h"

#include <SDL3/SDL.h>
#include <aurora/imgui.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gbbrowser {
namespace {

namespace fs = std::filesystem;

constexpr const char* kSite = "https://gamebanana.com";
constexpr int kPerPage = 30;
constexpr int kThumbW = 220, kThumbH = 124;   // a card's picture (GameBanana's 220-pixel screenshots, 16:9)
constexpr int kBigW = 530, kBigH = 298;       // the detail pane's

struct Category {
    const char* name;
    int id;
};
const Category kCategories[] = {{"Link", 10838}, {"All Skins", 10834}, {"Packs", 10837}, {"Other", 10835}};
struct Sort {
    const char* name;
    const char* key;
};
const Sort kSorts[] = {{"Most Downloaded", "Generic_MostDownloaded"}, {"Most Liked", "Generic_MostLiked"},
                       {"Newest", "Generic_Newest"}, {"Recently Updated", "Generic_LatestModified"}};

struct Mod {
    int id = 0;
    std::string name, author, profile, thumb;
    int likes = 0, views = 0;
};
struct File {
    int id = 0;
    std::string name, url, md5, description;
    uint64_t size = 0;
    bool clean = true;
};
struct Detail {
    int id = 0;
    std::string name, author, profile, text, license;
    int downloads = 0, likes = 0;
    std::vector<std::string> images;  // 530-pixel screenshots
    std::vector<File> files;
};
struct Download {
    std::atomic<uint64_t> got{0}, total{0};
    std::atomic<int> state{0};  // 0 running, 1 done (installed), 2 failed
    std::string error;          // (set before state)
};
// A picture: its pixels once a thread has them, its texture once the menu has made it.
struct Picture {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    std::atomic<int> state{0};  // 0 loading, 1 pixels ready, 2 failed
    ImTextureID tex = 0;
    int last_used = 0;
};

std::mutex g_lock;
std::vector<Mod> g_mods;
bool g_list_complete = false;
std::atomic<int> g_list_gen{0};
std::atomic<bool> g_list_loading{false};
std::string g_list_error;
std::shared_ptr<Detail> g_detail;
std::atomic<int> g_detail_want{0};
std::atomic<bool> g_detail_loading{false};
std::string g_detail_error;
std::map<std::string, std::shared_ptr<Picture>> g_pictures;  // by URL
std::map<int, std::shared_ptr<Download>> g_downloads;        // by file id
int g_frame = 0;

// the page's controls
char g_search[96] = "";
std::string g_search_applied;
int g_category = 0, g_sort = 0, g_page = 1, g_image = 0;
double g_search_changed = -1;
bool g_started = false;

// ---- the network ----
std::string url_encode(const std::string& s) {
    std::string out;
    char b[4];
    for (const unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
        else std::snprintf(b, sizeof b, "%%%02X", c), out += b;
    }
    return out;
}
// "https://host/path?query" as HttpRequest takes it.
bool split_url(const std::string& url, std::string& base, std::string& path) {
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos) return false;
    const size_t slash = url.find('/', scheme + 3);
    base = url.substr(0, slash);
    path = slash == std::string::npos ? "/" : url.substr(slash);
    return true;
}
bool get_json(const std::string& path, picojson::value& out, std::string& error) {
    const bodplat::HttpResult r = bodplat::HttpRequest(kSite, "GET", path, nullptr, 8u << 20);
    std::fprintf(stderr, "[models] GET %s: ok=%d status=%d %zu bytes\n", path.c_str(), r.ok ? 1 : 0, r.status, r.body.size());
    if (!r.ok || r.status != 200) {
        error = r.ok ? "GameBanana answered " + std::to_string(r.status) : "GameBanana could not be reached";
        return false;
    }
    if (const char* dump = std::getenv("BLUEWAKE_TEST_MODEL_BROWSER_DUMP"))
        if (FILE* f = std::fopen(dump, "wb")) std::fwrite(r.body.data(), 1, r.body.size(), f), std::fclose(f);
    error = picojson::parse(out, std::string(r.body.begin(), r.body.end()));
    if (!error.empty()) std::fprintf(stderr, "[models] JSON: %s\n", error.c_str());
    return error.empty();
}
const picojson::value& field(const picojson::value& v, const char* name) {
    static const picojson::value none;
    if (!v.is<picojson::object>()) return none;
    const auto& o = v.get<picojson::object>();
    const auto it = o.find(name);
    return it == o.end() ? none : it->second;
}
std::string str(const picojson::value& v) { return v.is<std::string>() ? v.get<std::string>() : std::string(); }
// A number (this picojson keeps whole numbers as 64-bit integers, PICOJSON_USE_INT64).
double numd(const picojson::value& v) {
#ifdef PICOJSON_USE_INT64
    if (v.is<int64_t>()) return double(v.get<int64_t>());
#endif
    return v.is<double>() ? v.get<double>() : 0.0;
}
int num(const picojson::value& v) { return int(numd(v)); }
bool is_webp(const std::string& file) {
    return file.size() >= 5 && (file.compare(file.size() - 5, 5, ".webp") == 0 || file.compare(file.size() - 5, 5, ".WEBP") == 0);
}
// The screenshots of a record's _aPreviewMedia, at a width (100, 220, 530 or 800), WebP ones left out.
std::vector<std::string> screenshots(const picojson::value& record, const char* size_field) {
    std::vector<std::string> out;
    const picojson::value& images = field(field(record, "_aPreviewMedia"), "_aImages");
    if (!images.is<picojson::array>()) return out;
    for (const auto& im : images.get<picojson::array>()) {
        std::string file = str(field(im, size_field));
        if (file.empty()) file = str(field(im, "_sFile"));
        if (file.empty() || is_webp(file)) continue;
        out.push_back(str(field(im, "_sBaseUrl")) + "/" + file);
    }
    return out;
}

void load_list() {
    const int gen = ++g_list_gen;
    g_list_loading = true;
    char path[512];
    std::snprintf(path, sizeof path, "/apiv11/Mod/Index?_nPerpage=%d&_nPage=%d&_aFilters%%5BGeneric_Category%%5D=%d&_sSort=%s", kPerPage,
                  g_page, kCategories[g_category].id, kSorts[g_sort].key);
    std::string p = path;
    if (!g_search_applied.empty()) p += "&_aFilters%5BGeneric_Name%5D=contains," + url_encode(g_search_applied);
    std::thread([gen, p] {
        picojson::value v;
        std::string error;
        std::vector<Mod> mods;
        bool complete = true;
        if (get_json(p, v, error)) {
            complete = field(field(v, "_aMetadata"), "_bIsComplete").evaluate_as_boolean();
            const picojson::value& records = field(v, "_aRecords");
            if (records.is<picojson::array>())
                for (const auto& r : records.get<picojson::array>()) {
                    Mod m;
                    m.id = num(field(r, "_idRow"));
                    m.name = str(field(r, "_sName"));
                    m.author = str(field(field(r, "_aSubmitter"), "_sName"));
                    m.profile = str(field(r, "_sProfileUrl"));
                    m.likes = num(field(r, "_nLikeCount"));
                    m.views = num(field(r, "_nViewCount"));
                    const auto shots = screenshots(r, "_sFile220");
                    if (!shots.empty()) m.thumb = shots.front();
                    if (m.id != 0) mods.push_back(std::move(m));
                }
        }
        std::fprintf(stderr, "[models] page: %zu models%s%s\n", mods.size(), error.empty() ? "" : ", ", error.c_str());
        if (gen != g_list_gen) return;  // another page was asked for meanwhile
        std::lock_guard<std::mutex> lock(g_lock);
        g_mods = std::move(mods);
        g_list_complete = complete;
        g_list_error = error;
        g_list_loading = false;
    }).detach();
}

// Text from GameBanana's HTML description: tags out, line breaks kept, the common entities read.
std::string plain_text(const std::string& html) {
    std::string out;
    for (size_t i = 0; i < html.size();) {
        if (html[i] == '<') {
            const size_t end = html.find('>', i);
            const std::string tag = html.substr(i, end == std::string::npos ? std::string::npos : end - i + 1);
            if (tag.rfind("<br", 0) == 0 || tag.rfind("</p", 0) == 0 || tag.rfind("<li", 0) == 0 || tag.rfind("</h", 0) == 0) out += '\n';
            if (tag.rfind("<li", 0) == 0) out += "- ";
            i = end == std::string::npos ? html.size() : end + 1;
            continue;
        }
        if (html[i] == '&') {
            static const std::pair<const char*, const char*> kEntities[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
                                                                           {"&quot;", "\""}, {"&#39;", "'"}, {"&nbsp;", " "}};
            bool done = false;
            for (const auto& [e, c] : kEntities)
                if (html.compare(i, std::strlen(e), e) == 0) {
                    out += c, i += std::strlen(e), done = true;
                    break;
                }
            if (done) continue;
        }
        out += html[i++];
    }
    // no runs of blank lines
    std::string tidy;
    int newlines = 0;
    for (const char c : out) {
        newlines = c == '\n' ? newlines + 1 : 0;
        if (newlines <= 2) tidy += c;
    }
    return tidy;
}

void load_detail(int id) {
    g_detail_want = id;
    g_detail_loading = true;
    g_image = 0;
    std::thread([id] {
        picojson::value v;
        std::string error;
        auto d = std::make_shared<Detail>();
        if (get_json("/apiv11/Mod/" + std::to_string(id) + "/ProfilePage", v, error)) {
            d->id = id;
            d->name = str(field(v, "_sName"));
            d->author = str(field(field(v, "_aSubmitter"), "_sName"));
            d->profile = str(field(v, "_sProfileUrl"));
            d->text = plain_text(str(field(v, "_sText")));
            d->license = str(field(v, "_sLicense"));
            d->downloads = num(field(v, "_nDownloadCount"));
            d->likes = num(field(v, "_nLikeCount"));
            d->images = screenshots(v, "_sFile530");
            const picojson::value& files = field(v, "_aFiles");
            if (files.is<picojson::array>())
                for (const auto& f : files.get<picojson::array>()) {
                    File file;
                    file.id = num(field(f, "_idRow"));
                    file.name = str(field(f, "_sFile"));
                    file.url = str(field(f, "_sDownloadUrl"));
                    file.md5 = str(field(f, "_sMd5Checksum"));
                    file.description = str(field(f, "_sDescription"));
                    file.size = uint64_t(numd(field(f, "_nFilesize")));
                    const std::string analysis = str(field(f, "_sAnalysisResult")), av = str(field(f, "_sAvResult"));
                    file.clean = (analysis.empty() || analysis == "ok") && (av.empty() || av == "clean");
                    if (file.id != 0 && !file.url.empty()) d->files.push_back(std::move(file));
                }
        }
        if (g_detail_want != id) return;
        std::lock_guard<std::mutex> lock(g_lock);
        g_detail = error.empty() ? d : nullptr;
        g_detail_error = error;
        g_detail_loading = false;
    }).detach();
}

// A picture, fetched and scaled into w x h (fitted, the rest clear) on a thread of its own.
std::shared_ptr<Picture> picture(const std::string& url, int w, int h) {
    const std::string key = url + "#" + std::to_string(w);
    std::lock_guard<std::mutex> lock(g_lock);
    auto& p = g_pictures[key];
    if (p) {
        p->last_used = g_frame;
        return p;
    }
    p = std::make_shared<Picture>();
    p->last_used = g_frame;
    std::shared_ptr<Picture> pic = p;
    std::thread([pic, url, w, h] {
        std::string base, path;
        bodplat::HttpResult r;
        if (split_url(url, base, path)) r = bodplat::HttpRequest(base, "GET", path, nullptr, 8u << 20);
        int iw = 0, ih = 0, n = 0;
        unsigned char* px = r.ok && r.status == 200 ? stbi_load_from_memory(r.body.data(), int(r.body.size()), &iw, &ih, &n, 4) : nullptr;
        if (px == nullptr) {
            pic->state = 2;
            return;
        }
        std::vector<uint8_t> out(size_t(w) * h * 4, 0);
        const float s = std::min(float(w) / float(iw), float(h) / float(ih));
        const int dw = std::max(1, int(float(iw) * s)), dh = std::max(1, int(float(ih) * s));
        const int ox = (w - dw) / 2, oy = (h - dh) / 2;
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x) {  // (box-filtered enough at these sizes: nearest from the centre of the cell)
                const int sx = std::min(iw - 1, int((float(x) + 0.5f) / s)), sy = std::min(ih - 1, int((float(y) + 0.5f) / s));
                std::memcpy(&out[(size_t(oy + y) * w + size_t(ox + x)) * 4], &px[(size_t(sy) * iw + size_t(sx)) * 4], 4);
            }
        stbi_image_free(px);
        pic->w = w, pic->h = h;
        pic->rgba = std::move(out);
        pic->state = 1;
    }).detach();
    return p;
}

// Textures recycled for pictures (the renderer has no way to free one): at most this many of each size.
struct Slot {
    ImTextureID tex = 0;
    Picture* owner = nullptr;
};
std::vector<Slot> g_thumb_slots, g_big_slots;
ImTextureID texture_for(Picture& p) {
    if (p.state != 1) return 0;
    if (p.tex != 0) return p.tex;
    std::vector<Slot>& slots = p.w == kThumbW ? g_thumb_slots : g_big_slots;
    const size_t limit = p.w == kThumbW ? 96 : 6;
    Slot* use = nullptr;
    if (slots.size() < limit) {
        slots.push_back({aurora_imgui_create_dynamic_texture(uint32_t(p.w), uint32_t(p.h)), nullptr});
        use = &slots.back();
    } else {  // the least recently drawn picture gives its texture up
        for (Slot& s : slots)
            if (use == nullptr || (s.owner ? s.owner->last_used : -1) < (use->owner ? use->owner->last_used : -1)) use = &s;
        if (use->owner) use->owner->tex = 0;
    }
    if (use->tex == 0) return 0;
    aurora_imgui_update_texture(use->tex, p.rgba.data());
    use->owner = &p;
    p.tex = use->tex;
    return p.tex;
}

// ---- MD5 (RFC 1321), to check a download against GameBanana's sum ----
std::string md5_of_file(const fs::path& path) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
        0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
        0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
        0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
        0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    auto block = [&](const uint8_t* p) {
        uint32_t m[16];
        for (int i = 0; i < 16; ++i) m[i] = uint32_t(p[i * 4]) | uint32_t(p[i * 4 + 1]) << 8 | uint32_t(p[i * 4 + 2]) << 16 | uint32_t(p[i * 4 + 3]) << 24;
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; ++i) {
            uint32_t f;
            int g;
            if (i < 16) f = (b & c) | (~b & d), g = i;
            else if (i < 32) f = (d & b) | (~d & c), g = (5 * i + 1) % 16;
            else if (i < 48) f = b ^ c ^ d, g = (3 * i + 5) % 16;
            else f = c ^ (b | ~d), g = (7 * i) % 16;
            const uint32_t t = d;
            d = c, c = b;
            const uint32_t x = a + f + K[i] + m[g];
            b = b + ((x << R[i]) | (x >> (32 - R[i])));
            a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d;
    };
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    uint64_t length = 0;
    std::vector<uint8_t> buf(1 << 16);
    std::vector<uint8_t> tail;
    while (in) {
        in.read(reinterpret_cast<char*>(buf.data()), std::streamsize(buf.size()));
        const size_t got = size_t(in.gcount());
        length += got;
        tail.insert(tail.end(), buf.begin(), buf.begin() + std::ptrdiff_t(got));
        size_t i = 0;
        for (; i + 64 <= tail.size(); i += 64) block(&tail[i]);
        tail.erase(tail.begin(), tail.begin() + std::ptrdiff_t(i));
    }
    tail.push_back(0x80);
    while (tail.size() % 64 != 56) tail.push_back(0);
    for (int i = 0; i < 8; ++i) tail.push_back(uint8_t((length * 8) >> (8 * i)));
    for (size_t i = 0; i < tail.size(); i += 64) block(&tail[i]);
    char out[33];
    for (int i = 0; i < 16; ++i) std::snprintf(out + i * 2, 3, "%02x", (h[i / 4] >> (8 * (i % 4))) & 0xFF);
    return out;
}

// A file name GameBanana gives, kept as is (so a download again is known) but made safe for the folder.
std::string safe_name(const std::string& name) {
    std::string out;
    for (const char c : name) out += (std::strchr("<>:\"/\\|?*", c) != nullptr || static_cast<unsigned char>(c) < 32) ? '_' : c;
    return out.empty() ? "download" : out;
}
fs::path installed_path(const File& f) { return fs::u8path(mods::Folder()) / fs::u8path(safe_name(f.name)); }

void start_download(const File& f) {
    auto d = std::make_shared<Download>();
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_downloads[f.id] = d;
    }
    const fs::path dest = installed_path(f);
    std::thread([d, f, dest] {
        std::error_code ec;
        fs::create_directories(dest.parent_path(), ec);
        const fs::path part = fs::path(dest).concat(".part");
        std::string error;
        const std::u8string part8 = part.u8string();
        bool ok = bodplat::HttpDownload(f.url, std::string(part8.begin(), part8.end()), [&](uint64_t got, uint64_t total) {
            d->got = got;
            d->total = total ? total : f.size;
            return true;
        }, &error);
        if (ok && !f.md5.empty()) {
            std::string sum = md5_of_file(part), want = f.md5;
            std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (sum != want) {
                ok = false, error = "the download does not match GameBanana's checksum";
                fs::remove(part, ec);
            }
        }
        if (ok) {
            fs::remove(dest, ec);
            fs::rename(part, dest, ec);
            if (ec) ok = false, error = "the file could not be put in the mods folder";
        }
        std::fprintf(stderr, "[models] %s: %s\n", f.name.c_str(), ok ? "installed" : error.c_str());
        d->error = error;
        d->state = ok ? 1 : 2;
    }).detach();
}

std::string size_text(uint64_t bytes) {
    char b[32];
    if (bytes >= 1024 * 1024) std::snprintf(b, sizeof b, "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    else std::snprintf(b, sizeof b, "%.0f KB", double(bytes) / 1024.0);
    return b;
}

// ---- the page ----
void draw_card(const Mod& m, float s, bool chosen) {
    ImGui::PushID(m.id);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float w = kThumbW * s, h = kThumbH * s, textH = ImGui::GetTextLineHeightWithSpacing() * 2.f + 6.f * s;
    const ImVec2 size(w + 12.f * s, h + textH + 12.f * s);
    if (ImGui::InvisibleButton("##card", size)) load_detail(m.id);
    const bool hot = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 paper = chosen ? IM_COL32(140, 180, 80, 255) : hot ? IM_COL32(200, 225, 150, 255) : IM_COL32(255, 248, 225, 235);
    dl->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), paper, 8.f * s);
    dl->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), IM_COL32(110, 75, 30, chosen ? 200 : 90), 8.f * s, 0, (chosen ? 2.f : 1.f) * s);
    const ImVec2 pic(at.x + 6.f * s, at.y + 6.f * s);
    dl->AddRectFilled(pic, ImVec2(pic.x + w, pic.y + h), IM_COL32(60, 45, 25, 60), 4.f * s);
    if (!m.thumb.empty()) {
        auto p = picture(m.thumb, kThumbW, kThumbH);
        if (const ImTextureID tex = texture_for(*p)) dl->AddImage(tex, pic, ImVec2(pic.x + w, pic.y + h));
    }
    ImGui::PushClipRect(at, ImVec2(at.x + size.x - 6.f * s, at.y + size.y), true);
    const ImU32 ink = IM_COL32(70, 45, 20, 255), faint = IM_COL32(70, 45, 20, 160);
    dl->AddText(ImVec2(pic.x, pic.y + h + 4.f * s), ink, m.name.c_str());
    char line[160];
    std::snprintf(line, sizeof line, "%s   \xE2\x99\xA5 %d", m.author.c_str(), m.likes);
    dl->AddText(ImVec2(pic.x, pic.y + h + 4.f * s + ImGui::GetTextLineHeightWithSpacing()), faint, line);
    ImGui::PopClipRect();
    ImGui::PopID();
}

void draw_detail(float s, float width) {
    std::shared_ptr<Detail> d;
    std::string error;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        d = g_detail;
        error = g_detail_error;
    }
    if (g_detail_loading) {
        ImGui::TextDisabled("Loading...");
        return;
    }
    if (!d) {
        if (!error.empty()) ImGui::TextColored(ImVec4(0.6f, 0.15f, 0.1f, 1), "%s", error.c_str());
        else ImGui::TextWrapped("Choose a model on the left to see its pictures, its files and its author's notes.");
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextUnformatted(d->name.c_str());
    ImGui::TextDisabled("by %s   %d downloads   %d likes", d->author.c_str(), d->downloads, d->likes);
    // pictures
    if (!d->images.empty()) {
        g_image = std::clamp(g_image, 0, int(d->images.size()) - 1);
        const float bw = std::min(width, kBigW * s), bh = bw * float(kBigH) / float(kBigW);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(bw, bh));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(at, ImVec2(at.x + bw, at.y + bh), IM_COL32(60, 45, 25, 60), 6.f * s);
        auto p = picture(d->images[size_t(g_image)], kBigW, kBigH);
        if (const ImTextureID tex = texture_for(*p)) dl->AddImage(tex, at, ImVec2(at.x + bw, at.y + bh));
        else if (p->state == 2) dl->AddText(ImVec2(at.x + 10 * s, at.y + 10 * s), IM_COL32(70, 45, 20, 200), "No picture");
        if (d->images.size() > 1) {
            if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) g_image = (g_image + int(d->images.size()) - 1) % int(d->images.size());
            ImGui::SameLine();
            ImGui::Text("%d / %d", g_image + 1, int(d->images.size()));
            ImGui::SameLine();
            if (ImGui::ArrowButton("##next", ImGuiDir_Right)) g_image = (g_image + 1) % int(d->images.size());
        }
    }
    // files
    ImGui::Spacing();
    ImGui::SeparatorText("Files");
    for (const File& f : d->files) {
        ImGui::PushID(f.id);
        ImGui::TextUnformatted(f.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", size_text(f.size).c_str());
        if (!f.description.empty()) ImGui::TextDisabled("%s", f.description.c_str());
        std::shared_ptr<Download> dl;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            if (auto it = g_downloads.find(f.id); it != g_downloads.end()) dl = it->second;
        }
        std::error_code ec;
        const bool installed = fs::exists(installed_path(f), ec);
        if (!f.clean) {
            ImGui::TextColored(ImVec4(0.6f, 0.15f, 0.1f, 1), "GameBanana's file check did not pass: not offered.");
        } else if (dl && dl->state == 0) {
            const float frac = dl->total ? float(double(dl->got) / double(dl->total)) : 0.f;
            const std::string label = size_text(dl->got) + (dl->total ? " of " + size_text(dl->total) : std::string());
            ImGui::ProgressBar(frac, ImVec2(-1, 0), label.c_str());
        } else {
            if (dl && dl->state == 2) ImGui::TextColored(ImVec4(0.6f, 0.15f, 0.1f, 1), "%s", dl->error.c_str());
            if (ImGui::Button(installed ? "Download Again" : "Download & Install", ImVec2(-1, 0))) start_download(f);
            if (installed) ImGui::TextDisabled("In your mods folder: Mod Menu lists it (the top one is worn).");
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
    if (d->files.empty()) ImGui::TextDisabled("This mod has no files to download here.");
    // the author's words, the licence, the page
    ImGui::SeparatorText("About");
    if (!d->license.empty()) ImGui::TextDisabled("Licence: %s", plain_text(d->license).c_str());
    ImGui::TextUnformatted(d->text.c_str());
    ImGui::Spacing();
    if (!d->profile.empty() && ImGui::Button("Open on GameBanana", ImVec2(-1, 0))) SDL_OpenURL(d->profile.c_str());
    ImGui::PopTextWrapPos();
}

// Pictures not drawn for a while are let go (their textures go back to the slots).
void prune_pictures() {
    if (g_frame % 300 != 0) return;
    std::lock_guard<std::mutex> lock(g_lock);
    for (auto it = g_pictures.begin(); it != g_pictures.end();) {
        Picture& p = *it->second;
        if (p.state != 0 && g_frame - p.last_used > 600) {
            for (auto* slots : {&g_thumb_slots, &g_big_slots})
                for (Slot& slot : *slots)
                    if (slot.owner == &p) slot.owner = nullptr;
            it = g_pictures.erase(it);
        } else {
            ++it;
        }
    }
}

void draw_page(float s) {
    ++g_frame;
    prune_pictures();
    if (!g_started) {
        g_started = true;
        load_list();
    }
    // a download that has finished: the list again from the folder, the new model on top and in effect
    {
        std::vector<int> done;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            for (auto& [id, d] : g_downloads)
                if (d->state == 1) done.push_back(id);
        }
        if (!done.empty()) {
            mods::RescanAndApply();
            std::lock_guard<std::mutex> lock(g_lock);
            for (int id : done) g_downloads.erase(id);
        }
    }
    // the controls
    ImGui::SetNextItemWidth(240.f * s);
    if (ImGui::InputTextWithHint("##search", "Search models...", g_search, sizeof g_search)) g_search_changed = ImGui::GetTime();
    if (g_search_changed >= 0 && ImGui::GetTime() - g_search_changed > 0.6) {  // (asked once the typing stops)
        g_search_changed = -1;
        g_search_applied = g_search;
        g_page = 1;
        load_list();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170.f * s);
    if (ImGui::BeginCombo("##sort", kSorts[g_sort].name)) {
        for (int i = 0; i < int(sizeof kSorts / sizeof kSorts[0]); ++i)
            if (ImGui::Selectable(kSorts[i].name, i == g_sort)) g_sort = i, g_page = 1, load_list();
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.f * s);
    if (ImGui::BeginCombo("##category", kCategories[g_category].name)) {
        for (int i = 0; i < int(sizeof kCategories / sizeof kCategories[0]); ++i)
            if (ImGui::Selectable(kCategories[i].name, i == g_category)) g_category = i, g_page = 1, load_list();
        ImGui::EndCombo();
    }
    bool complete;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        complete = g_list_complete;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(g_page <= 1 || g_list_loading);
    if (ImGui::ArrowButton("##prevpage", ImGuiDir_Left)) --g_page, load_list();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("Page %d", g_page);
    ImGui::SameLine();
    ImGui::BeginDisabled(complete || g_list_loading);
    if (ImGui::ArrowButton("##nextpage", ImGuiDir_Right)) ++g_page, load_list();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("from GameBanana");

    // the grid and the chosen one
    const float avail = ImGui::GetContentRegionAvail().x;
    const float detailW = std::max(300.f * s, avail * 0.38f);
    const float gridW = avail - detailW - ImGui::GetStyle().ItemSpacing.x;
    const float height = std::max(200.f * s, ImGui::GetContentRegionAvail().y);
    ImGui::BeginChild("##grid", ImVec2(gridW, height), ImGuiChildFlags_None);
    {
        std::vector<Mod> mods;
        std::string error;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            mods = g_mods;
            error = g_list_error;
        }
        if (g_list_loading && mods.empty()) ImGui::TextDisabled("Loading...");
        else if (!error.empty()) ImGui::TextColored(ImVec4(0.6f, 0.15f, 0.1f, 1), "%s", error.c_str());
        else if (mods.empty()) ImGui::TextDisabled("No models match.");
        const float cardW = (kThumbW + 12.f) * s + ImGui::GetStyle().ItemSpacing.x;
        const int columns = std::max(1, int((gridW - 8.f * s) / cardW));
        int col = 0;
        std::shared_ptr<Detail> d;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            d = g_detail;
        }
        const int chosen = g_detail_loading ? g_detail_want.load() : d ? d->id : 0;
        for (const Mod& m : mods) {
            if (col++ % columns != 0) ImGui::SameLine();
            draw_card(m, s, m.id == chosen);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##detail", ImVec2(detailW, height), ImGuiChildFlags_None);
    draw_detail(s, detailW - 16.f * s);
    ImGui::EndChild();
}

} // namespace

void DrawEmbedded(float scale) { draw_page(scale); }

void Frame(float scale, float fontScale) {
    // BLUEWAKE_TEST_MODEL_BROWSER (testing): the first page asked for at start, its answer logged
    static bool tested;
    if (!tested && std::getenv("BLUEWAKE_TEST_MODEL_BROWSER") != nullptr) {
        tested = g_started = true;
        load_list();
    }
    if (CVarGetInteger("gOpenWindows.ModelBrowser", 0) == 0) return;
    shipmenu::PopoutWindow("Browse Models", "gOpenWindows.ModelBrowser", ImVec2(1000 * scale, 640 * scale), 0.97f, fontScale,
                           [&] { draw_page(scale); });
}

} // namespace gbbrowser
