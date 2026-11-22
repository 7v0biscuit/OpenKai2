// SPDX-License-Identifier: GPL-3.0-only
#include "retroachievements.h"
#include "disc_image.h"
#include "launcher/version.h"
#include <rc_api_runtime.h>
#include <rc_api_user.h>
#include <rc_client.h>
#include <rc_consoles.h>
#include <rc_hash.h>
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace openkai2::retro {
namespace {

std::mutex transport_mutex;
Transport custom_transport;

std::string user_agent() { return std::string("OpenKai2/") + launcher::application_version; }

#ifdef _WIN32
std::wstring widen(const std::string &text) {
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(size_t(std::max(size, 0)), L'\0');
    if (size > 0)
        MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size);
    return out;
}

Http windows_send(const std::string &url, const std::string &post, const std::string &type) {
    Http result;
    const auto wide = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{}, path[2048]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    wchar_t extra[2048]{};
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts))
        return result;
    const auto agent = widen(user_agent());
    HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return result;
    WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    const std::wstring target = std::wstring(path) + extra;
    HINTERNET request = connection ? WinHttpOpenRequest(connection, post.empty() ? L"GET" : L"POST", target.c_str(), nullptr,
                                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                   : nullptr;
    if (request) {
        const std::wstring header = post.empty() ? std::wstring() : L"Content-Type: " + widen(type.empty() ? "application/x-www-form-urlencoded" : type) + L"\r\n";
        if (WinHttpSendRequest(request, header.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header.c_str(), DWORD(-1L),
                               post.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char *>(post.data()), DWORD(post.size()), DWORD(post.size()), 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            result.status = int(status);
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
                    break;
                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0)
                    break;
                result.body.append(chunk.data(), read);
                if (result.body.size() > 16u * 1024u * 1024u)
                    break;
            }
        }
        WinHttpCloseHandle(request);
    }
    if (connection)
        WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}

void *RC_CCONV open_file(const char *path) { return _wfopen(widen(path).c_str(), L"rb"); }
#else
void *RC_CCONV open_file(const char *path) { return std::fopen(path, "rb"); }
#endif

void RC_CCONV seek_file(void *handle, int64_t offset, int origin) {
#ifdef _WIN32
    _fseeki64(static_cast<FILE *>(handle), offset, origin);
#else
    fseeko(static_cast<FILE *>(handle), off_t(offset), origin);
#endif
}
int64_t RC_CCONV tell_file(void *handle) {
#ifdef _WIN32
    return _ftelli64(static_cast<FILE *>(handle));
#else
    return int64_t(ftello(static_cast<FILE *>(handle)));
#endif
}
size_t RC_CCONV read_file(void *handle, void *buffer, size_t bytes) { return std::fread(buffer, 1, bytes, static_cast<FILE *>(handle)); }
void RC_CCONV close_file(void *handle) { std::fclose(static_cast<FILE *>(handle)); }

template <class Fill>
Http call(Fill fill) {
    rc_api_request_t request{};
    if (fill(request) != RC_OK)
        return {};
    const std::string url = request.url ? request.url : "", post = request.post_data ? request.post_data : "",
                      type = request.content_type ? request.content_type : "";
    rc_api_destroy_request(&request);
    return send(url, post, type);
}

rc_api_server_response_t server(const Http &http) {
    rc_api_server_response_t response{};
    response.body = http.body.c_str();
    response.body_length = http.body.size();
    response.http_status_code = http.status ? http.status : RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    return response;
}

std::string text(const char *value) { return value ? value : ""; }

std::string escape(const std::string &value) {
    std::string out;
    for (const char c : value)
        out += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    return out;
}

}

unsigned SetInfo::points() const {
    unsigned total = 0;
    for (const auto &entry : entries)
        total += entry.points;
    return total;
}

unsigned SetInfo::earned() const {
    unsigned total = 0;
    for (const auto &entry : entries)
        if (entry.unlocked)
            total += entry.points;
    return total;
}

void set_transport(Transport transport) {
    std::lock_guard lock(transport_mutex);
    custom_transport = std::move(transport);
}

Http send(const std::string &url, const std::string &post, const std::string &type) {
    Transport transport;
    {
        std::lock_guard lock(transport_mutex);
        transport = custom_transport;
    }
    if (transport)
        return transport(url, post, type);
#ifdef _WIN32
    return windows_send(url, post, type);
#else
    return {};
#endif
}

std::string disc_hash(const std::filesystem::path &image) {
    static std::once_flag once;
    std::call_once(once, [] {
        static rc_hash_filereader reader{open_file, seek_file, tell_file, read_file, close_file};
        rc_hash_init_custom_filereader(&reader);
        rc_hash_init_default_cdreader();
    });
    char hash[33]{};
    const auto path = image.u8string();
    const std::string utf8(path.begin(), path.end());
    return rc_hash_generate_from_file(hash, RC_CONSOLE_PLAYSTATION_2, utf8.c_str()) ? std::string(hash) : std::string();
}

LoginResult login(const std::string &user, const std::string &password) {
    LoginResult result;
    rc_api_login_request_t params{};
    params.username = user.c_str();
    params.password = password.c_str();
    const auto http = call([&](rc_api_request_t &request) { return rc_api_init_login_request(&request, &params); });
    if (!http.status) {
        result.error = "Could not reach RetroAchievements.";
        return result;
    }
    rc_api_login_response_t response{};
    const auto raw = server(http);
    const int code = rc_api_process_login_server_response(&response, &raw);
    if (code == RC_OK && response.response.succeeded && response.api_token) {
        result.ok = true;
        result.user = text(response.username);
        result.token = text(response.api_token);
        result.score = response.score;
        result.softcore = response.score_softcore;
    } else {
        result.error = response.response.error_message ? response.response.error_message : "Sign-in failed.";
    }
    rc_api_destroy_login_response(&response);
    return result;
}

Lookup resolve(const std::string &hash) {
    Lookup result;
    rc_api_resolve_hash_request_t params{};
    params.game_hash = hash.c_str();
    const auto http = call([&](rc_api_request_t &request) { return rc_api_init_resolve_hash_request(&request, &params); });
    if (!http.status) {
        result.error = "Could not reach RetroAchievements.";
        return result;
    }
    rc_api_resolve_hash_response_t response{};
    const auto raw = server(http);
    if (rc_api_process_resolve_hash_server_response(&response, &raw) == RC_OK && response.response.succeeded) {
        result.ok = true;
        result.game = response.game_id;
    } else {
        result.error = response.response.error_message ? response.response.error_message : "RetroAchievements did not answer the lookup.";
    }
    rc_api_destroy_resolve_hash_response(&response);
    return result;
}

SetInfo fetch_set(const std::string &user, const std::string &token, unsigned game) {
    SetInfo set;
    set.game = game;
    rc_api_fetch_game_sets_request_t params{};
    params.username = user.c_str();
    params.api_token = token.c_str();
    params.game_id = game;
    const auto http = call([&](rc_api_request_t &request) { return rc_api_init_fetch_game_sets_request(&request, &params); });
    if (!http.status) {
        set.error = "Could not reach RetroAchievements.";
        return set;
    }
    rc_api_fetch_game_sets_response_t response{};
    const auto raw = server(http);
    if (rc_api_process_fetch_game_sets_server_response(&response, &raw) == RC_OK && response.response.succeeded) {
        set.ok = true;
        set.title = text(response.title);
        for (uint32_t s = 0; s < response.num_sets; ++s) {
            const auto &group = response.sets[s];
            if (group.type != RC_ACHIEVEMENT_SET_TYPE_CORE)
                continue;
            for (uint32_t i = 0; i < group.num_achievements; ++i) {
                const auto &item = group.achievements[i];
                if (item.category != RC_ACHIEVEMENT_CATEGORY_CORE)
                    continue;
                Entry entry;
                entry.id = item.id;
                entry.points = item.points;
                entry.title = text(item.title);
                entry.description = text(item.description);
                entry.badge = text(item.badge_name);
                set.entries.push_back(std::move(entry));
            }
        }
    } else {
        set.error = response.response.error_message ? response.response.error_message : "RetroAchievements did not send the set.";
    }
    rc_api_destroy_fetch_game_sets_response(&response);
    return set;
}

std::filesystem::path cache_path() { return default_cache_directory().parent_path() / "retroachievements.txt"; }

SetInfo load_cache(const std::filesystem::path &file) {
    SetInfo set;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> fields;
        std::stringstream row(line);
        std::string field;
        while (std::getline(row, field, '\t'))
            fields.push_back(field);
        if (fields.empty())
            continue;
        try {
            if (fields[0] == "game" && fields.size() >= 4) {
                set.game = unsigned(std::stoul(fields[1]));
                set.hash = fields[2];
                set.title = fields[3];
                set.ok = true;
            } else if (fields[0] == "entry" && fields.size() >= 9) {
                Entry entry;
                entry.id = unsigned(std::stoul(fields[1]));
                entry.points = unsigned(std::stoul(fields[2]));
                entry.unlocked = fields[3] == "1";
                entry.hardcore = fields[4] == "1";
                entry.when = std::stoll(fields[5]);
                entry.badge = fields[6];
                entry.title = fields[7];
                entry.description = fields[8];
                set.entries.push_back(std::move(entry));
            }
        } catch (const std::exception &) {
        }
    }
    return set;
}

void save_cache(const std::filesystem::path &file, const SetInfo &set) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    const auto temporary = file.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << "game\t" << set.game << '\t' << escape(set.hash) << '\t' << escape(set.title) << '\n';
        for (const auto &entry : set.entries)
            out << "entry\t" << entry.id << '\t' << entry.points << '\t' << (entry.unlocked ? 1 : 0) << '\t' << (entry.hardcore ? 1 : 0) << '\t'
                << entry.when << '\t' << escape(entry.badge) << '\t' << escape(entry.title) << '\t' << escape(entry.description) << '\n';
        if (!out)
            return;
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::filesystem::remove(file, error);
        std::filesystem::rename(temporary, file, error);
    }
}

struct Session::State {
    Config config;
    rc_client_t *client = nullptr;
    mutable std::mutex mutex;
    std::vector<Event> events;
    std::string status = "Signing in to RetroAchievements...";
    bool playing = false;
    struct Job {
        std::string url, post, type;
        rc_client_server_callback_t callback;
        void *data;
    };
    std::mutex jobs_mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    bool stopping = false;
    std::thread worker;

    void post(Event event) {
        std::lock_guard lock(mutex);
        events.push_back(std::move(event));
    }
    void set_status(std::string value) {
        std::lock_guard lock(mutex);
        status = std::move(value);
    }
    void run() {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(jobs_mutex);
                wake.wait(lock, [this] { return stopping || !jobs.empty(); });
                if (stopping)
                    return;
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            const auto http = send(job.url, job.post, job.type);
            const auto raw = server(http);
            job.callback(&raw, job.data);
        }
    }
    void remember() {
        if (!client || config.cache.empty())
            return;
        const auto *game = rc_client_get_game_info(client);
        if (!game || !game->id)
            return;
        SetInfo set;
        set.ok = true;
        set.game = game->id;
        set.hash = config.hash;
        set.title = text(game->title);
        if (auto *list = rc_client_create_achievement_list(client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE)) {
            for (uint32_t b = 0; b < list->num_buckets; ++b)
                for (uint32_t i = 0; i < list->buckets[b].num_achievements; ++i) {
                    const auto *item = list->buckets[b].achievements[i];
                    Entry entry;
                    entry.id = item->id;
                    entry.points = item->points;
                    entry.title = text(item->title);
                    entry.description = text(item->description);
                    entry.badge = item->badge_name;
                    entry.unlocked = item->unlocked != 0;
                    entry.hardcore = (item->unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_HARDCORE) != 0;
                    entry.when = int64_t(item->unlock_time);
                    set.entries.push_back(std::move(entry));
                }
            rc_client_destroy_achievement_list(list);
        }
        save_cache(config.cache, set);
    }
};

namespace {
Session::State *owner(rc_client_t *client) { return static_cast<Session::State *>(rc_client_get_userdata(client)); }

uint32_t RC_CCONV read_memory(uint32_t address, uint8_t *buffer, uint32_t bytes, rc_client_t *client) {
    auto *state = owner(client);
    return state && state->config.read ? state->config.read(address, buffer, bytes) : 0u;
}

void RC_CCONV server_call(const rc_api_request_t *request, rc_client_server_callback_t callback, void *data, rc_client_t *client) {
    auto *state = owner(client);
    if (!state)
        return;
    Session::State::Job job{text(request->url), text(request->post_data), text(request->content_type), callback, data};
    {
        std::lock_guard lock(state->jobs_mutex);
        state->jobs.push_back(std::move(job));
    }
    state->wake.notify_one();
}

void RC_CCONV handle_event(const rc_client_event_t *event, rc_client_t *client) {
    auto *state = owner(client);
    if (!state)
        return;
    switch (event->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
        if (event->achievement) {
            Event out;
            out.kind = Event::Kind::unlocked;
            out.title = text(event->achievement->title);
            out.description = text(event->achievement->description);
            out.points = event->achievement->points;
            state->post(std::move(out));
            state->remember();
        }
        break;
    case RC_CLIENT_EVENT_GAME_COMPLETED: {
        Event out;
        out.kind = Event::Kind::completed;
        if (const auto *game = rc_client_get_game_info(client))
            out.title = text(game->title);
        state->post(std::move(out));
        break;
    }
    case RC_CLIENT_EVENT_DISCONNECTED:
        state->post({Event::Kind::message, "RetroAchievements is unreachable. Unlocks will be sent when it is back.", "", 0});
        break;
    case RC_CLIENT_EVENT_RECONNECTED:
        state->post({Event::Kind::message, "RetroAchievements reconnected. Pending unlocks were sent.", "", 0});
        break;
    case RC_CLIENT_EVENT_SERVER_ERROR:
        if (event->server_error && event->server_error->error_message)
            state->post({Event::Kind::message, std::string("RetroAchievements: ") + event->server_error->error_message, "", 0});
        break;
    default:
        break;
    }
}

void RC_CCONV game_loaded(int result, const char *error, rc_client_t *client, void *) {
    auto *state = owner(client);
    if (!state)
        return;
    const auto *game = rc_client_get_game_info(client);
    if (result == RC_OK && game && game->id) {
        unsigned total = 0, earned = 0;
        if (auto *list = rc_client_create_achievement_list(client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE)) {
            for (uint32_t b = 0; b < list->num_buckets; ++b)
                for (uint32_t i = 0; i < list->buckets[b].num_achievements; ++i) {
                    ++total;
                    if (list->buckets[b].achievements[i]->unlocked)
                        ++earned;
                }
            rc_client_destroy_achievement_list(list);
        }
        {
            std::lock_guard lock(state->mutex);
            state->playing = true;
        }
        state->set_status(text(game->title) + ": " + std::to_string(earned) + " of " + std::to_string(total) + " RetroAchievements earned");
        state->post({Event::Kind::message, "RetroAchievements: " + std::to_string(earned) + " of " + std::to_string(total) + " earned" +
                                               (state->config.hardcore ? " (hardcore)" : ""), "", 0});
        state->remember();
    } else if (result == RC_NO_GAME_LOADED || (result == RC_OK && (!game || !game->id))) {
        state->set_status("This disc is not linked on RetroAchievements yet.");
        state->post({Event::Kind::message, "RetroAchievements: this disc is not linked yet. Built-in achievements are still on.", "", 0});
    } else {
        state->set_status(std::string("RetroAchievements: ") + (error ? error : "could not load the game."));
        state->post({Event::Kind::message, std::string("RetroAchievements: ") + (error ? error : "could not load the game."), "", 0});
    }
}

void RC_CCONV logged_in(int result, const char *error, rc_client_t *client, void *) {
    auto *state = owner(client);
    if (!state)
        return;
    if (result != RC_OK) {
        state->set_status(std::string("RetroAchievements sign-in failed: ") + (error ? error : "unknown error"));
        state->post({Event::Kind::message, "RetroAchievements sign-in failed. Sign in again on the Achievements page.", "", 0});
        return;
    }
    state->set_status("Loading RetroAchievements...");
    rc_client_begin_load_game(client, state->config.hash.c_str(), game_loaded, nullptr);
}
}

Session::Session(Config config) : state_(std::make_unique<State>()) {
    state_->config = std::move(config);
    state_->worker = std::thread([state = state_.get()] { state->run(); });
    state_->client = rc_client_create(read_memory, server_call);
    if (!state_->client) {
        state_->set_status("RetroAchievements could not start.");
        return;
    }
    rc_client_set_userdata(state_->client, state_.get());
    rc_client_set_event_handler(state_->client, handle_event);
    rc_client_set_hardcore_enabled(state_->client, state_->config.hardcore ? 1 : 0);
    rc_client_set_unofficial_enabled(state_->client, 0);
    if (state_->config.user.empty() || state_->config.token.empty() || state_->config.hash.empty()) {
        state_->set_status("Sign in to RetroAchievements on the Achievements page.");
        return;
    }
    rc_client_begin_login_with_token(state_->client, state_->config.user.c_str(), state_->config.token.c_str(), logged_in, nullptr);
}

Session::~Session() {
    {
        std::lock_guard lock(state_->jobs_mutex);
        state_->stopping = true;
    }
    state_->wake.notify_all();
    if (state_->worker.joinable())
        state_->worker.join();
    if (state_->client)
        rc_client_destroy(state_->client);
}

void Session::frame() {
    if (!state_->client)
        return;
    if (rc_client_is_game_loaded(state_->client))
        rc_client_do_frame(state_->client);
    else
        rc_client_idle(state_->client);
}

std::vector<Event> Session::take() {
    std::lock_guard lock(state_->mutex);
    std::vector<Event> out;
    out.swap(state_->events);
    return out;
}

std::string Session::status() const {
    std::lock_guard lock(state_->mutex);
    return state_->status;
}

bool Session::playing() const {
    std::lock_guard lock(state_->mutex);
    return state_->playing;
}

}
