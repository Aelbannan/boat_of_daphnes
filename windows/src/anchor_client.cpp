// The online play connection: see anchor_client.h.
#include "anchor_client.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace anchor {
namespace {

std::mutex g_lock;
std::deque<std::string> g_outbox, g_inbox;
std::string g_error;
std::atomic<Status> g_status{Status::Disconnected};

// One Start's connection. Stop only flags it and lets go: its thread closes the socket and ends on its own, so the
// caller (the render thread) never waits on the network. A WebSocket close on a dead link waited ~10 s for the
// server's answer, and the game froze that long at a disconnect (a friend's log, 2026-10-12).
struct Conn {
    std::atomic<bool> run{true};
    Options options;
    unsigned id = 0;
};
std::shared_ptr<Conn> g_conn;
unsigned g_conn_id = 0; // the current connection's id, under g_lock

bool current(const Conn& c) {
    std::lock_guard lock(g_lock);
    return c.id == g_conn_id;
}

void set_status(const Conn& c, Status s) {
    if (current(c))
        g_status = s;
}

std::wstring widen(const std::string& s) {
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

void fail(const Conn& c, const char* what) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s (error %lu)", what, GetLastError());
    std::lock_guard lock(g_lock);
    if (c.id != g_conn_id)
        return;
    g_error = buf;
    g_status = Status::Failed;
    std::fprintf(stderr, "[anchor] %s\n", buf);
}

// One connection, until it drops or Stop. Returns when the socket is gone.
void session(Conn& c) {
    const Options& options = c.options;
    HINTERNET session = WinHttpOpen(L"boat-of-daphnes/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
        return fail(c, "cannot start WinHTTP");
    // Connect, send and receive give up after 10 s instead of WinHTTP's 30-60 s.
    WinHttpSetTimeouts(session, 10000, 10000, 10000, 10000);
    HINTERNET connect = WinHttpConnect(session, widen(options.host).c_str(), options.port, 0);
    const std::wstring path = widen("/ws?room=" + url_encode(options.room));
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                     options.secure ? WINHTTP_FLAG_SECURE : 0)
                                : nullptr;
    HINTERNET socket = nullptr;
    if (request == nullptr) {
        fail(c, "cannot reach the server");
    } else if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
               !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
               !WinHttpReceiveResponse(request, nullptr)) {
        fail(c, "the server did not answer");
    } else if ((socket = WinHttpWebSocketCompleteUpgrade(request, 0)) == nullptr) {
        fail(c, "the server refused the connection");
    }
    if (request)
        WinHttpCloseHandle(request);
    if (socket != nullptr && !c.run) {
        WinHttpCloseHandle(socket); // stopped while connecting
        socket = nullptr;
    }
    if (socket != nullptr) {
        set_status(c, Status::Connected);
        std::fprintf(stderr, "[anchor] connected to %s room\n", options.host.c_str());
        // A reader thread; this thread writes and pings.
        std::atomic<bool> open{true};
        std::thread reader([&] {
            std::string message;
            std::vector<char> buffer(16 * 1024);
            while (open) {
                DWORD read = 0;
                WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
                if (WinHttpWebSocketReceive(socket, buffer.data(), static_cast<DWORD>(buffer.size()), &read, &type) !=
                    NO_ERROR) {
                    open = false;
                    break;
                }
                if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                    open = false;
                    break;
                }
                message.append(buffer.data(), read);
                if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                    std::lock_guard lock(g_lock);
                    if (c.id == g_conn_id && message != "{\"type\":\"PONG\"}")
                        g_inbox.push_back(std::move(message));
                    message.clear();
                } else if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
                    message.clear(); // reserved for a later pose format
                }
            }
        });
        auto last_ping = std::chrono::steady_clock::now();
        while (c.run && open) {
            std::deque<std::string> out;
            {
                std::lock_guard lock(g_lock);
                if (c.id == g_conn_id)
                    out.swap(g_outbox);
            }
            for (const std::string& m : out)
                if (WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                         const_cast<char*>(m.data()), static_cast<DWORD>(m.size())) != NO_ERROR) {
                    open = false;
                    break;
                }
            const auto now = std::chrono::steady_clock::now();
            if (now - last_ping > std::chrono::seconds(30)) {
                static const char ping[] = "{\"type\":\"PING\"}";
                WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, const_cast<char*>(ping),
                                     sizeof ping - 1);
                last_ping = now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        open = false;
        // Send the close frame without waiting for the server's (WinHttpWebSocketClose waits), then close the handle,
        // which ends the reader's pending receive.
        WinHttpWebSocketShutdown(socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        WinHttpCloseHandle(socket);
        reader.join();
        if (current(c) && g_status == Status::Connected)
            g_status = c.run ? Status::Connecting : Status::Disconnected;
        std::fprintf(stderr, "[anchor] disconnected\n");
    }
    if (connect)
        WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
}

void run(std::shared_ptr<Conn> c) {
    int backoff = 1;
    while (c->run) {
        set_status(*c, Status::Connecting);
        session(*c);
        if (!c->run)
            break;
        // Reconnect with a growing wait (1, 2, 4 ... 30 s), checking for Stop.
        for (int i = 0; i < backoff * 10 && c->run; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        backoff = std::min(backoff * 2, 30);
    }
}

} // namespace

void Start(const Options& options) {
    Stop();
    auto c = std::make_shared<Conn>();
    c->options = options;
    {
        std::lock_guard lock(g_lock);
        c->id = ++g_conn_id;
        g_error.clear();
        g_inbox.clear();
        g_outbox.clear();
    }
    g_status = Status::Connecting;
    g_conn = c;
    std::thread(run, c).detach();
}

void Stop() {
    if (g_conn) {
        g_conn->run = false;
        g_conn.reset();
    }
    {
        std::lock_guard lock(g_lock);
        ++g_conn_id; // the old thread's late status, inbox and outbox use are ignored
        g_outbox.clear();
    }
    g_status = Status::Disconnected;
}

Status GetStatus() { return g_status; }

std::string LastError() {
    std::lock_guard lock(g_lock);
    return g_error;
}

void Send(const std::string& json) {
    std::lock_guard lock(g_lock);
    g_outbox.push_back(json);
}

std::vector<std::string> Poll() {
    std::lock_guard lock(g_lock);
    std::vector<std::string> out(g_inbox.begin(), g_inbox.end());
    g_inbox.clear();
    return out;
}

} // namespace anchor
