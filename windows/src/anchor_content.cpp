// See anchor_content.h.
#include <cstdio>
#undef rename // bw_posix_compat.h; <filesystem> declares rename()
#include <filesystem>

#include "anchor_content.h"

#include "mods.h"
#include "rarc.h"

#define PICOJSON_USE_INT64
#include "picojson.h"

#include "platform/bod_platform.h"

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

namespace content {
namespace {

namespace fs = std::filesystem;

constexpr size_t kMaxFile = 20u * 1024 * 1024;
constexpr size_t kMaxPack = 20u * 1024 * 1024;
constexpr size_t kMaxManifest = 64u * 1024;
constexpr uintmax_t kCacheLimit = 500ull * 1024 * 1024;

std::mutex g_mutex;
std::condition_variable g_wake;
std::deque<std::function<void()>> g_jobs;
std::thread g_worker;
bool g_stop = false;

std::string g_base;
std::string g_local_hash;
bool g_local_changed = false;
std::string g_published_model = "\x01"; // never a model's name: forces the first check
std::string g_published_base;
bool g_publishing = false;
bool g_was_sharing = false;
unsigned long long g_last_check = 0;

struct Peer {
    std::string hash;
    bool fetching = false, done = false, failed = false;
};
std::map<int, Peer> g_peers;
std::vector<Ready> g_ready;
std::vector<int> g_cleared;

fs::path cache_dir() {
    static const fs::path dir = [] {
        fs::path p = bodplat::DataFolder();
        if (p.empty())
            p = fs::current_path();
        return p / "cache" / "anchor-content";
    }();
    return dir;
}

void worker_main() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_wake.wait(lock, [] { return g_stop || !g_jobs.empty(); });
            if (g_stop)
                return;
            job = std::move(g_jobs.front());
            g_jobs.pop_front();
        }
        job();
    }
}

} // namespace

void Shutdown() {
    // The worker is a std::thread still joinable at exit, whose destructor calls std::terminate (a crash report at every
    // quit). It is told to stop and let go: a download in progress ends with the process.
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_stop = true;
        g_wake.notify_all();
    }
    if (g_worker.joinable())
        g_worker.detach();
}

namespace {

void enqueue(std::function<void()> job) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_worker.joinable())
        g_worker = std::thread(worker_main);
    g_jobs.push_back(std::move(job));
    g_wake.notify_one();
}

// ----------------------------------------------------------------- HTTP ----

using Http = bodplat::HttpResult;

Http request(const std::string& base, const char* method, const std::string& path, const std::vector<uint8_t>* body,
             size_t max_response) {
    return bodplat::HttpRequest(base, method, path, body, max_response);
}

// ---------------------------------------------------------------- cache ----

bool read_file(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

void touch(const fs::path& p) {
    std::error_code ec;
    fs::last_write_time(p, fs::file_time_type::clock::now(), ec);
}

// Oldest files go first until the cache fits.
void trim_cache() {
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    uintmax_t total = 0;
    for (fs::directory_iterator it(cache_dir(), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2))
            continue;
        total += it->file_size(e2);
        files.emplace_back(it->last_write_time(e2), it->path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& [time, path] : files) {
        if (total <= kCacheLimit)
            break;
        std::error_code e2;
        const uintmax_t size = fs::file_size(path, e2);
        if (fs::remove(path, e2))
            total -= std::min(total, size);
    }
}

void store(const std::string& hash, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    fs::create_directories(cache_dir(), ec);
    const fs::path tmp = cache_dir() / (hash + ".part");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const fs::path dest = cache_dir() / hash;
    fs::rename(tmp, dest, ec); // replaces an existing file, on Windows too (MoveFileEx with REPLACE_EXISTING)
    if (ec)
        fs::remove(tmp, ec);
}

bool is_hex64(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// A file by hash: the cache's, or downloaded and checked against the hash and size.
bool fetch_blob(const std::string& base, const std::string& hash, size_t size, std::vector<uint8_t>& out) {
    if (!is_hex64(hash) || size > kMaxFile)
        return false;
    const fs::path cached = cache_dir() / hash;
    if (read_file(cached, out) && out.size() == size && Sha256Hex(out.data(), out.size()) == hash) {
        touch(cached);
        return true;
    }
    const Http h = request(base, "GET", "/content/" + hash, nullptr, kMaxFile + 1024);
    if (!h.ok || h.status != 200 || h.body.size() != size || Sha256Hex(h.body.data(), h.body.size()) != hash)
        return false;
    out = h.body;
    store(hash, out);
    trim_cache();
    return true;
}

// ------------------------------------------------------------ publishing ----

void publish(std::string base, std::string model) {
    std::string name;
    std::vector<mods::PackFile> files;
    std::string hash;
    if (!model.empty() && mods::ActivePlayerPack(name, files)) {
        // The server stores only Wind Waker archives (RARC, plain or Yaz0), so a pack is shared as its Link.arc
        // and any equipment archives: metadata, colour masks and voice files stay on this PC (voice banks have
        // no header to check, and a puppet cannot play them yet).
        files.erase(std::remove_if(files.begin(), files.end(),
                                   [](const mods::PackFile& f) {
                                       const auto& b = f.bytes;
                                       const bool archive = b.size() >= 16 && (std::equal(b.begin(), b.begin() + 4, "RARC") || std::equal(b.begin(), b.begin() + 4, "Yaz0"));
                                       return !archive || (f.role != "model" && f.role != "equipment");
                                   }),
                    files.end());
        // Over the pack limit: the voice goes first, then the equipment.
        auto total = [&] {
            size_t t = 0;
            for (const auto& f : files)
                t += f.bytes.size();
            return t;
        };
        for (const char* drop : {"voice", "equipment"})
            if (total() > kMaxPack)
                files.erase(std::remove_if(files.begin(), files.end(), [&](const mods::PackFile& f) { return f.role == drop; }), files.end());
        if (total() <= kMaxPack && !files.empty()) {
            std::sort(files.begin(), files.end(), [](const mods::PackFile& a, const mods::PackFile& b) { return a.path < b.path; });
            picojson::array list;
            std::vector<std::pair<std::string, const mods::PackFile*>> sums;
            for (const auto& f : files) {
                picojson::object o;
                const std::string h = Sha256Hex(f.bytes.data(), f.bytes.size());
                o["path"] = picojson::value(f.path);
                o["role"] = picojson::value(f.role);
                o["sha256"] = picojson::value(h);
                o["size"] = picojson::value(static_cast<int64_t>(f.bytes.size()));
                list.push_back(picojson::value(o));
                sums.emplace_back(h, &f);
            }
            picojson::object m;
            m["v"] = picojson::value(int64_t{1});
            m["name"] = picojson::value(name.substr(0, 60));
            m["files"] = picojson::value(list);
            const std::string text = picojson::value(m).serialize();
            const std::vector<uint8_t> manifest(text.begin(), text.end());
            const std::string manifest_hash = Sha256Hex(manifest.data(), manifest.size());
            bool ok = manifest.size() <= kMaxManifest;
            // Only what the server lacks goes up.
            auto upload = [&](const std::string& h, const std::vector<uint8_t>& bytes, const char* kind) {
                const Http head = request(base, "HEAD", "/content/" + h, nullptr, 0);
                if (head.ok && head.status == 200)
                    return true;
                const Http put = request(base, "PUT", "/content/" + h + (kind ? std::string("?kind=") + kind : ""), &bytes, 4096);
                if (!put.ok || (put.status != 201 && put.status != 204)) {
                    std::fprintf(stderr, "[content] upload of %.12s failed (%d)\n", h.c_str(), put.status);
                    return false;
                }
                return true;
            };
            for (const auto& [h, f] : sums)
                if (ok && !upload(h, f->bytes, nullptr))
                    ok = false;
            if (ok && upload(manifest_hash, manifest, "manifest")) {
                hash = manifest_hash;
                std::fprintf(stderr, "[content] sharing %s: %zu files, %zu bytes, manifest %.12s\n", name.c_str(), files.size(), total(),
                             hash.c_str());
            }
        } else
            std::fprintf(stderr, "[content] %s is over %zu MB: not shared\n", name.c_str(), kMaxPack >> 20);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_local_hash != hash) {
        g_local_hash = hash;
        g_local_changed = true;
    }
    g_publishing = false;
}

// -------------------------------------------------------------- fetching ----

void fetch_peer(std::string base, int id, std::string hash) {
    std::vector<uint8_t> body;
    bool ok = false;
    std::vector<uint8_t> manifest;
    std::fprintf(stderr, "[content] fetching manifest %.12s for player %d\n", hash.c_str(), id);
    if ([&] {
            // The manifest's size is not known: it is checked by its hash alone.
            const fs::path cached = cache_dir() / hash;
            if (read_file(cached, manifest) && manifest.size() <= kMaxManifest && Sha256Hex(manifest.data(), manifest.size()) == hash) {
                touch(cached);
                return true;
            }
            const Http h = request(base, "GET", "/content/" + hash, nullptr, kMaxManifest + 1);
            if (!h.ok || h.status != 200 || h.body.empty() || Sha256Hex(h.body.data(), h.body.size()) != hash)
                return false;
            manifest = h.body;
            store(hash, manifest);
            return true;
        }()) {
        picojson::value v;
        const std::string text(manifest.begin(), manifest.end());
        if (picojson::parse(v, text).empty() && v.is<picojson::object>() && v.contains("files") && v.get("files").is<picojson::array>()) {
            size_t total = 0;
            std::string link_hash;
            size_t link_size = 0;
            for (const auto& e : v.get("files").get<picojson::array>()) {
                if (!e.is<picojson::object>() || !e.contains("path") || !e.contains("sha256") || !e.contains("size") || !e.get("size").is<int64_t>())
                    continue;
                const std::string path = e.get("path").to_str();
                const int64_t size = e.get("size").get<int64_t>();
                if (size < 0 || static_cast<size_t>(size) > kMaxFile || path.find("..") != std::string::npos)
                    continue;
                total += static_cast<size_t>(size);
                if (path == "Link.arc") {
                    link_hash = e.get("sha256").to_str();
                    link_size = static_cast<size_t>(size);
                }
            }
            std::fprintf(stderr, "[content] manifest %.12s: %zu bytes of files, Link.arc %.12s (%zu bytes)\n", hash.c_str(), total, link_hash.c_str(), link_size);
            // Only the body model is used for a puppet (the voice, equipment and colors are not yet): only it is fetched.
            std::vector<uint8_t> arc;
            if (total <= kMaxPack && !link_hash.empty() && fetch_blob(base, link_hash, link_size, arc)) {
                rarc::Yaz0Expand(arc);
                ok = rarc::ValidateLinkArc(arc, &body);
                if (!ok)
                    std::fprintf(stderr, "[content] Link.arc %.12s is not a usable Link.arc (%zu bytes)\n", link_hash.c_str(), arc.size());
            }
        }
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_peers.find(id);
    if (it != g_peers.end() && it->second.hash == hash) {
        it->second.fetching = false;
        it->second.done = ok;
        it->second.failed = !ok;
        if (ok) {
            Ready r;
            r.id = id;
            r.hash = hash;
            r.body = std::move(body);
            g_ready.push_back(std::move(r));
            std::fprintf(stderr, "[content] player %d's model %.12s is ready\n", id, hash.c_str());
        } else
            std::fprintf(stderr, "[content] player %d's content %.12s could not be used: their puppet is Link's own\n", id, hash.c_str());
    }
}

} // namespace

std::string Sha256Hex(const uint8_t* data, size_t size) {
    uint8_t digest[32] = {0};
    if (!bodplat::Sha256(data, size, digest))
        return "";
    static const char hex[] = "0123456789abcdef";
    std::string out;
    for (const uint8_t b : digest) {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}

void Tick(const std::string& http_base, bool share, bool connected) {
    const unsigned long long now = bodplat::TickMs();
    std::string model;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_base = http_base;
        if (!share || !connected) {
            // Not sharing: nothing of ours is advertised, and nobody's is used.
            if (g_was_sharing) {
                if (!g_local_hash.empty())
                    g_local_changed = true;
                g_local_hash.clear();
                g_published_model = "\x01";
                for (auto& [id, p] : g_peers) {
                    if (p.done)
                        g_cleared.push_back(id);
                    p = Peer{};
                }
                g_ready.clear();
            }
            g_was_sharing = false;
            return;
        }
        g_was_sharing = true;
        if (g_publishing || now - g_last_check < 1500)
            return;
        g_last_check = now;
        model = mods::ActiveModel();
        if (model == g_published_model && http_base == g_published_base)
            return;
        g_published_model = model;
        g_published_base = http_base;
        g_publishing = true;
    }
    enqueue([http_base, model] { publish(http_base, model); });
}

std::string LocalHash() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_local_hash;
}

bool LocalChanged() {
    std::lock_guard<std::mutex> lock(g_mutex);
    const bool c = g_local_changed;
    g_local_changed = false;
    return c;
}

void Want(int id, const std::string& hash) {
    std::string base;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_was_sharing)
            return;
        Peer& p = g_peers[id];
        if (p.hash == hash)
            return;
        if (p.done)
            g_cleared.push_back(id);
        p = Peer{};
        p.hash = hash;
        if (hash.empty() || !is_hex64(hash))
            return;
        p.fetching = true;
        base = g_base;
    }
    enqueue([base, id, hash] { fetch_peer(base, id, hash); });
}

void Forget(int id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_peers.find(id);
    if (it != g_peers.end()) {
        if (it->second.done)
            g_cleared.push_back(id);
        g_peers.erase(it);
    }
}

std::vector<Ready> TakeReady() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return std::exchange(g_ready, {});
}

std::vector<int> TakeCleared() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return std::exchange(g_cleared, {});
}

} // namespace content
