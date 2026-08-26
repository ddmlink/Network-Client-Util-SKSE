#pragma once
#include <unordered_map>
#include "helper_functions.h"
#include "logger.h"
#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

inline std::atomic<bool> g_httpClientEnabled{true};
inline std::atomic<bool> g_websocketClientEnabled{true};
inline std::atomic<bool> g_httpLoggingEnabled { false };
inline constexpr const char* iniPath = "Data/SKSE/Plugins/NetworkClientUtil.ini";

// this was originally a .cpp file, but it turns out that not following proper .h/.cpp patterns
// can cause issues as you add more files to the project. This is now a header, and I had to mark 
// 'UrlEncode' as inline to prevent linker issues

inline std::string UrlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (unsigned char c : value) {
        // Unreserved characters per RFC
        if (std::isalnum(c) || c == '-') {
            escaped << c;
        } else if (c == ' ') {
            escaped << "%20";
        } else {
            escaped << '%' << std::setw(2) << std::uppercase << static_cast<int>(c) << std::nouppercase;
        }
    }

    return escaped.str();
}

struct HttpResult {
    std::string requestTag; // ID provided by the caller
    int statusCode;
    std::string body; // raw json
};

class HttpLogBuffer {
public:
    void Add(const std::string& line) {
        if (!g_httpLoggingEnabled.load(std::memory_order_relaxed)) {
            return;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        _lines.push_back(line);

        // size limit
        if (_lines.size() > 200) {
            _lines.pop_front();
        }
    }

    std::deque<std::string> GetSnapshot() { 
        std::lock_guard<std::mutex> lock(_mutex); 
        return _lines;
    }

    void Clear() { 
        std::lock_guard<std::mutex> lock(_mutex);
        _lines.clear();
    }

private:
    std::mutex _mutex;
    std::deque<std::string> _lines;
};

inline HttpLogBuffer g_httpLog;

class WebSocketLogBuffer {
public:
    void Add(const std::string& tag, const std::string& line, bool force = false) {
        if (!IsEnabled(tag) && !force) {
            return;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        auto& lines = _logs[tag];
        lines.push_back(line);

        if (lines.size() > 200) {
            lines.pop_front();
        }
    }

    std::deque<std::string> GetSnapshot(const std::string& tag) { 
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _logs.find(tag);
        return it != _logs.end() ? it->second : std::deque<std::string>{};
    }

    void Clear(const std::string& tag) { 
        std::lock_guard<std::mutex> lock(_mutex);
        _logs.erase(tag);
    }

    void SetEnabled(const std::string& tag, bool enabled) { 
        std::lock_guard<std::mutex> lock(_enabledMutex);
        _enabled[tag] = enabled;
    }

    bool IsEnabled(const std::string& tag) { 
        std::lock_guard<std::mutex> lock(_enabledMutex);
        auto it = _enabled.find(tag);
        return it != _enabled.end() && it->second;
    }

private:
    std::mutex _mutex;
    std::unordered_map<std::string, std::deque<std::string>> _logs;
    std::mutex _enabledMutex;
    std::unordered_map <std::string, bool> _enabled;
};

inline WebSocketLogBuffer g_wsLog;

class HttpResultQueue {
public:
    void Push(HttpResult&& result) { 
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push_back(std::move(result));
    }

    void DrainQueue(size_t maxItems) {
        std::deque<HttpResult> local;
        { 
            std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);
            if (!lock.owns_lock()) return;

            size_t n = std::min(maxItems, _queue.size());
            for (size_t i = 0; i < n; ++i) {
                local.push_back(std::move(_queue.front()));
                _queue.pop_front();
            }
        }
        for (auto& r : local) {
            SendModEvent(r);
        }
    }

private:
    void SendModEvent(const HttpResult& r) { 
        auto* modCallbackSource = SKSE::GetModCallbackEventSource();
        if (!modCallbackSource) {
            return;
        }

        // send SKSE mod event - ID given when Papyrus call was made, raw JSON, return code, NONE
        SKSE::ModCallbackEvent event{r.requestTag, r.body, static_cast<float>(r.statusCode), nullptr};
        modCallbackSource->SendEvent(&event);
    }

    std::mutex _mutex;
    std::deque<HttpResult> _queue;
};

inline HttpResultQueue g_httpResults;

class HttpService {
public:
    void Init() { 
        ix::initNetSystem();  // this must be initialized for Windows devices before we can use ixwebsocket
        _client = std::make_unique<ix::HttpClient>(true); // we only need 1 instance of our client. Set to true to enable async
    }

    void GetAsync(const std::string& url, const std::string& tag, const std::vector<std::string>& headers) { 
        if (!CheckClientEnabled(tag)) {
            return;
        }

        auto args = _client->createRequest(url);
        args->connectTimeout = 5;
        args->transferTimeout = 10;

        for (const auto& line : headers) {
            std::string key, value;
            if (ParseHeaderLine(line, key, value)) {
                args->extraHeaders[key] = value;
            } else {
                logger::warn("Skipping malformed header: '{}'", line);
            }
        }

        g_httpLog.Add("[GET] " + url + " tag='" + tag + "'");

        _client->performRequest(args, [tag, url](const ix::HttpResponsePtr& response) {
            HttpResult result;
            result.requestTag = tag;
            result.statusCode = response ? response->statusCode : 0;
            result.body = response ? response->body : "";

            g_httpLog.Add("[RESPONSE] " + url + " tag='" + tag + "' status=" + std::to_string(result.statusCode) + " body=" + result.body);
            g_httpResults.Push(std::move(result));
        });
    }

    void GetWithParams(const std::string& url, const std::vector<std::string>& fields, const std::string& tag, const std::vector<std::string>& headers) {
        if (!CheckClientEnabled(tag)) {
            return;
        }
        
        std::string fullUrl = url;
        bool first = true;

        for (const auto& line : fields) {
            std::string key, value;

            if (!ParseHeaderLine(line, key, value)) {
                logger::warn("Skipping malformed query field: '{}'", line);
                continue;
            }

            fullUrl += (first ? "?" : "&");
            fullUrl += UrlEncode(key) + "=" + UrlEncode(value);
            first = false;
        }
        
        auto args = _client->createRequest(fullUrl, ix::HttpClient::kGet);
        args->connectTimeout = 5;
        args->transferTimeout = 10;

        for (const auto& line : headers) {
            std::string key, value;
            if (ParseHeaderLine(line, key, value)) {
                args->extraHeaders[key] = value;
            } else {
                logger::warn("Skipping malformed header: '{}'", line);
            }
        }

        g_httpLog.Add("[GET] " + url + " tag='" + tag + "'");

        _client->performRequest(args, [tag, url](const ix::HttpResponsePtr& response) {
            HttpResult result;
            result.requestTag = tag;
            result.statusCode = response ? response->statusCode : 0;
            result.body = response ? response->body : "";

            g_httpLog.Add("[RESPONSE] " + url + " tag='" + tag + "' status=" + std::to_string(result.statusCode) + " body=" + result.body);
            g_httpResults.Push(std::move(result));
        });
    }

    void PostAsync(const std::string& url, const std::string& jsonBody, const std::string& tag, std::vector<std::string>& headers) {
        if (!CheckClientEnabled(tag)) {
            return;
        }
        
        auto args = _client->createRequest(url, ix::HttpClient::kPost);
        args->connectTimeout = 5;
        args->transferTimeout = 10;

        args->extraHeaders["Content-Type"] = "application/json"; // default JSON headers

        for (const auto& line : headers) {
            std::string key, value;
            if (ParseHeaderLine(line, key, value)) {
                args->extraHeaders[key] = value; // this will allow Content-Type to be overridden if included
            } else {
                logger::warn("Skipping malformed header line: '{}'", line);
            }
        }

        args->body = jsonBody;
        
        g_httpLog.Add("[POST] " + url + " tag='" + tag + "' body=" + jsonBody);

        _client->performRequest(args, [tag, url](const ix::HttpResponsePtr& response) { 
            HttpResult result;
            result.requestTag = tag;
            result.statusCode = response ? response->statusCode : 0;
            result.body = response ? response->body : "";

            //logger::info("POST result for '{}': status={}, body='{}'", tag, result.statusCode, result.body);
            g_httpLog.Add("[RESPONSE] " + url + " tag='" + tag + "' status=" + std::to_string(result.statusCode) + " body=" + result.body);

            g_httpResults.Push(std::move(result));
        });
    }

    void PostParamsAsync(const std::string& url, const std::vector<std::string>& fields, const std::string& tag, const std::vector<std::string>& headers) {
        if (!CheckClientEnabled(tag)) {
            return;
        }
        
        auto args = _client->createRequest(url, ix::HttpClient::kPost);
        args->connectTimeout = 5;
        args->transferTimeout = 10;
        ix::HttpParameters params;

        args->extraHeaders["Content-Type"] = "application/x-www-form-urlencoded";  // default url-encoded headers

        for (const auto& line : fields) {
            std::string key, value;

            if (ParseHeaderLine(line, key, value)) {
                params[key] = value;
            } else {
                logger::warn("Skipping malformed form field: '{}'", line);
            }
        }

        args->body = _client->serializeHttpParameters(params);

        g_httpLog.Add("[POST] " + url + " tag='" + tag + "' body=" + args->body);
        
        for (const auto& line : headers) {
            std::string key, value;
            if (ParseHeaderLine(line, key, value)) {
                args->extraHeaders[key] = value;  // once again, this will allow Content-Type to be overridden if included
            } else {
                logger::warn("Skipping malformed header line: '{}'", line);
            }
        }

        _client->performRequest(args, [tag, url](const ix::HttpResponsePtr& response) {
            HttpResult result;
            result.requestTag = tag;
            result.statusCode = response ? response->statusCode : 0;
            result.body = response ? response->body : "";

            g_httpLog.Add("[RESPONSE] " + url + " tag='" + tag + "' status=" + std::to_string(result.statusCode) + " body=" + result.body);

            g_httpResults.Push(std::move(result));
        });
    }

private:
    std::unique_ptr<ix::HttpClient> _client;

    bool CheckClientEnabled(const std::string& tag) { 
        if (g_httpClientEnabled.load(std::memory_order_relaxed)) {
            return true; // enabled, so good to send message
        }

        // send failure result so we know player has disabled http client
        HttpResult result;
        result.requestTag = tag;
        result.statusCode = -200;
        result.body = "{\"error\": \"HTTP client is disabled by the player\"}";
        g_httpResults.Push(std::move(result));

        return false;
    }
};

inline HttpService g_httpService;

class WebSocketService {
public:
    void Connect(const std::string& url, const std::string& tag, const std::vector<std::string>& filters = {}, const std::vector<std::string>& headers = {}) { 
        if (!g_websocketClientEnabled.load(std::memory_order_relaxed)) {
            HttpResult result;
            result.requestTag = tag;
            result.statusCode = -200;
            result.body = "{\"error\": \"WebSocket client is disabled by the player\"}";
            g_httpResults.Push(std::move(result));
            return;
        }

        std::lock_guard<std::mutex> lock(_socketsMutex); 

        // if we connect to another site or session using the same tag, we need to disconnect and clean up the current session before we replace it
        auto existing = _sockets.find(tag);
        if (existing != _sockets.end()) {
            existing->second->stop();
            _sockets.erase(existing);
        }

        SetConnected(tag, false); // don't mark as connected until we actually connect
        SetFilters(tag, filters); 

        auto socket = std::make_unique<ix::WebSocket>();
        socket->setUrl(url);
        socket->setPingInterval(30);

        if (!headers.empty()) {
            ix::WebSocketHttpHeaders wsHeaders;
            for (const auto& line : headers) {
                std::string key, value;
                if (ParseHeaderLine(line, key, value)) {
                    wsHeaders[key] = value;
                } else {
                    logger::warn("Skipping malformed WebSocket header: {}", line);
                }
            }
            socket->setExtraHeaders(wsHeaders);
        }

        g_wsLog.Add(tag, "[CONNECT] to: " + url, true); // ALWAYS log the initial connection

        socket->setOnMessageCallback([this, tag](const ix::WebSocketMessagePtr& msg) {
            switch (msg->type) {
                case ix::WebSocketMessageType::Open:
                    logger::info("WebSocket connected: {}", tag);
                    SetConnected(tag, true);
                    break;
                case ix::WebSocketMessageType::Message: {
                    g_wsLog.Add(tag, "[MESSAGE] " + msg->str);

                    if (!PassesFilter(tag, msg->str)) {
                        break;
                    }

                    HttpResult result;
                    result.requestTag = tag;
                    result.statusCode = 200;  // return the same success code as the http client for easy checks
                    result.body = msg->str;   // this is the JSON response that you get
                    g_httpResults.Push(std::move(result));
                    break;
                }
                case ix::WebSocketMessageType::Ping: // IXWebSocket automatically handles ping/pong messages. Yay!
                    //logger::trace("Ping received from {}", tag);
                    g_wsLog.Add(tag, "[PING]");
                    break;
                case ix::WebSocketMessageType::Pong:
                    //logger::trace("Pong received from {}", tag);
                    g_wsLog.Add(tag, "[PONG]");
                    break;
                case ix::WebSocketMessageType::Close:
                    logger::warn("WebSocket closed: {} (code {})", tag, msg->closeInfo.code);
                    SetConnected(tag, false);
                    g_wsLog.Add(tag, "[CLOSE] code " + std::to_string(msg->closeInfo.code));
                    break;
                case ::ix::WebSocketMessageType::Error:
                    logger::error("WebSocket error on {}: {}", tag, msg->errorInfo.reason);
                    SetConnected(tag, false);
                    g_wsLog.Add(tag, "[ERROR] " + msg->errorInfo.reason);
                    break;
            }
        });

        socket->start();
        _sockets[tag] = std::move(socket);  
        // ^^^ if we are already connected to a site with this tag (such as Twitch) and we connect to another site
        // with this tag, then replace the Twitch connection with the new site or socket
    }

    void Disconnect(const std::string& tag) { 
        std::lock_guard<std::mutex> lock(_socketsMutex);
        auto it = _sockets.find(tag);
        if (it != _sockets.end()) {
            it->second->stop();
            _sockets.erase(it);
        }
        SetConnected(tag, false);
        ClearFilters(tag);
    }

    void Send(const std::string& tag, const std::string& message) { 
        std::lock_guard<std::mutex> lock(_socketsMutex);
        auto it = _sockets.find(tag);
        
        if (it == _sockets.end()) {
            logger::warn("Websocket send failed - no active connection for tag '{}'", tag);
            PushSendFailure(tag, "No active connection.");
            return;
        }

        auto info = it->second->send(message);
        if (!info.success) {
            logger::warn("Websocket send failed for tag '{}' - message may not have been delivered", tag);
            PushSendFailure(tag, "Send failed");
        }
    }

    bool IsConnected(const std::string& tag) { 
        std::lock_guard<std::mutex> lock(_stateMutex);
        auto it = _connected.find(tag);
        return it != _connected.end() && it->second;
    }

    void UpdateFilters(const std::string& tag, const std::vector<std::string>& filters) { 
        SetFilters(tag, filters);
    }

    std::vector<std::string> GetConnectedTags() { 
        std::lock_guard<std::mutex> lock(_stateMutex);
        std::vector<std::string> tags;

        for (const auto& [tag, connected] : _connected) {
            if (connected) {
                tags.push_back(tag);
            }
        }

        return tags;
    }

private:
    void SetConnected(const std::string& tag, bool value) { 
        std::lock_guard<std::mutex> lock(_stateMutex); 
        _connected[tag] = value;
    }

    void SetFilters(const std::string& tag, const std::vector<std::string>& filters) {
        std::lock_guard<std::mutex> lock(_filtersMutex);
        _filters[tag] = filters;
    }

    void ClearFilters(const std::string& tag) { 
        std::lock_guard<std::mutex> lock(_filtersMutex);
        _filters.erase(tag);
    }

    bool PassesFilter(const std::string& tag, const std::string& message) {
        std::lock_guard<std::mutex> lock(_filtersMutex);
        auto it = _filters.find(tag);

        if (it == _filters.end() || it->second.empty()) {
            return true; // no filters, so just return true
        }

        for (const auto& filter : it->second) {
            if (ContainsCaseInsensitive(message, filter)) {
                return true; // found string in response
            }
        }

        return false;
    }

    void PushSendFailure(const std::string& tag, const std::string& reason) { 
        HttpResult result;
        result.requestTag = tag;
        result.statusCode = -1; // Real status codes aren't negative, so this shouldn't conflict
        result.body = reason;
        g_httpResults.Push(std::move(result));
    }

        std::mutex _socketsMutex;
        std::unordered_map<std::string, std::unique_ptr<ix::WebSocket>> _sockets;

        std::mutex _stateMutex;
        std::unordered_map<std::string, bool> _connected;

        std::mutex _filtersMutex;
        std::unordered_map < std::string, std::vector<std::string>> _filters;
};

inline WebSocketService g_webSocketService;
