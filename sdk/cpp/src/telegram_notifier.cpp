#include "agentforge/telegram_notifier.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <curl/curl.h>
#include <iomanip>
#include <random>
#include <sstream>
#include <thread>

namespace agentforge::telegram_notifier {
namespace {

struct CurlGlobal {
  CurlGlobal() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
      throw std::runtime_error("curl global initialization failed");
    }
  }
  ~CurlGlobal() { curl_global_cleanup(); }
};

CurlGlobal& curl_global() {
  static CurlGlobal value;
  return value;
}

std::size_t write_callback(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string replace_all(std::string value, const std::string& needle, std::string_view replacement) {
  if (needle.empty()) return value;
  std::size_t pos = 0;
  while ((pos = value.find(needle, pos)) != std::string::npos) {
    value.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
  return value;
}

std::string redact(std::string value, const std::string& credential) {
  return credential.empty() ? value : replace_all(std::move(value), credential, "[REDACTED]");
}

std::string make_idempotency_key() {
  std::random_device random;
  std::array<unsigned int, 4> words{random(), random(), random(), random()};
  std::ostringstream out;
  out << "cpp-";
  for (auto word : words) {
    out << std::hex << std::setw(8) << std::setfill('0') << word;
  }
  return out.str();
}

bool host_prefix(const std::string& origin, std::string_view prefix) {
  if (origin.rfind(prefix, 0) != 0) return false;
  if (origin.size() == prefix.size()) return true;
  const char next = origin[prefix.size()];
  return next == ':' || next == '/';
}

bool is_loopback_http(const std::string& origin) {
  return host_prefix(origin, "http://127.0.0.1") ||
         host_prefix(origin, "http://localhost") ||
         host_prefix(origin, "http://[::1]");
}

void validate_origin(const std::string& origin) {
  const bool secure = origin.rfind("https://", 0) == 0;
  const bool loopback = is_loopback_http(origin);
  if (!secure && !loopback) {
    throw std::invalid_argument("remote origins must use HTTPS; HTTP is allowed only for loopback");
  }
  if (origin.find('@') != std::string::npos ||
      origin.find('?') != std::string::npos ||
      origin.find('#') != std::string::npos) {
    throw std::invalid_argument("origin must not contain credentials, query, or fragment");
  }
  const auto scheme_end = origin.find("://");
  const auto path = origin.find('/', scheme_end == std::string::npos ? 0 : scheme_end + 3);
  if (path != std::string::npos && path != origin.size() - 1) {
    throw std::invalid_argument("origin must be a bare HTTP(S) origin");
  }
}

void validate_source(const std::string& source) {
  if (source.empty() || source.size() > 200) {
    throw std::invalid_argument("source must be between 1 and 200 characters");
  }
  for (unsigned char c : source) {
    if (std::iscntrl(c)) throw std::invalid_argument("source must not contain control characters");
  }
}

void validate_header_value(const std::string& value, std::string_view name) {
  for (unsigned char c : value) {
    if (std::iscntrl(c)) {
      throw std::invalid_argument(std::string(name) + " must not contain control characters");
    }
  }
}

void validate_event_id(const std::string& event_id) {
  if (event_id.empty() || event_id.size() > 300 ||
      event_id.find('/') != std::string::npos ||
      event_id.find('\\') != std::string::npos) {
    throw std::invalid_argument("invalid event id");
  }
}

void validate_filename(const std::string& filename) {
  if (filename.empty() || filename.find('/') != std::string::npos ||
      filename.find('\\') != std::string::npos ||
      filename.find('\r') != std::string::npos ||
      filename.find('\n') != std::string::npos) {
    throw std::invalid_argument("invalid filename");
  }
}

void validate_public_route(const std::string& method, const std::string& path) {
  if (path.rfind("/v1/", 0) != 0 ||
      path.find('?') != std::string::npos ||
      path.find('#') != std::string::npos ||
      path.find('\\') != std::string::npos ||
      path.find("//") != std::string::npos ||
      path.find("/../") != std::string::npos ||
      path.find("/./") != std::string::npos) {
    throw std::invalid_argument("only safe Telegram Notifier public v1 routes are allowed");
  }
  const auto lowered = lower(path);
  if (lowered.find("%2f") != std::string::npos ||
      lowered.find("%5c") != std::string::npos ||
      lowered.find("%2e") != std::string::npos ||
      lowered.find("%25") != std::string::npos) {
    throw std::invalid_argument("encoded path separators and traversal are forbidden");
  }

  const bool allowed =
      (method == "GET" && (path == "/v1/inbox" || path == "/v1/file")) ||
      (method == "POST" && (path == "/v1/notify" || path == "/v1/reply"));
  if (!allowed) {
    throw std::invalid_argument("only Telegram Notifier public v1 routes are allowed");
  }
}

std::string escape(CURL* curl, const std::string& value) {
  char* encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
  if (!encoded) throw std::runtime_error("URL escaping failed");
  std::string result(encoded);
  curl_free(encoded);
  return result;
}

bool retryable(long status) {
  return status == 408 || status == 425 || status == 429 ||
         status == 500 || status == 502 || status == 503 || status == 504;
}

std::optional<long long> integer_field(const nlohmann::json& a,
                                       const nlohmann::json& b,
                                       std::string_view camel,
                                       std::string_view snake) {
  for (const auto* value : {&a, &b}) {
    for (const auto key : {camel, snake}) {
      if (value->contains(key) && (*value)[key].is_number_integer()) {
        return (*value)[key].get<long long>();
      }
    }
  }
  return std::nullopt;
}

std::string string_field(const nlohmann::json& a,
                         const nlohmann::json& b,
                         std::initializer_list<std::string_view> keys) {
  for (const auto* value : {&a, &b}) {
    for (const auto key : keys) {
      if (value->contains(key) && (*value)[key].is_string()) {
        return (*value)[key].get<std::string>();
      }
    }
  }
  return {};
}

[[noreturn]] void throw_error(long status, const std::string& body,
                              const std::string& credential) {
  std::string code = "HTTP_ERROR";
  std::string message = "Telegram Notifier request failed.";
  nlohmann::json root = nlohmann::json::object();
  nlohmann::json detail = nlohmann::json::object();

  try {
    root = nlohmann::json::parse(body.empty() ? "{}" : body);
    detail = root;
    if (root.contains("error")) {
      if (root["error"].is_object()) {
        detail = root["error"];
      } else if (root["error"].is_string()) {
        message = root["error"].get<std::string>();
      }
    }
    if (detail.contains("code") && detail["code"].is_string()) code = detail["code"].get<std::string>();
    if (detail.contains("message") && detail["message"].is_string()) message = detail["message"].get<std::string>();
  } catch (...) {
    if (!body.empty()) message = body;
  }

  auto request_id = string_field(detail, root, {"requestId", "request_id"});
  auto upgrade_url = string_field(detail, root, {"upgradeUrl", "upgrade_url"});
  auto account_url = string_field(detail, root, {"accountUrl", "account_url"});
  auto quota_reset_at = string_field(detail, root, {"quotaResetAt", "quota_reset_at"});
  const auto retry_value = integer_field(detail, root, "retryAfterSeconds", "retry_after_seconds");
  const auto quota_used = integer_field(detail, root, "quotaUsed", "quota_used");
  const auto quota_limit = integer_field(detail, root, "quotaLimit", "quota_limit");
  std::optional<int> retry_after;
  if (retry_value) retry_after = static_cast<int>(*retry_value);

  code = redact(code, credential);
  message = redact(message, credential);
  request_id = redact(request_id, credential);
  upgrade_url = redact(upgrade_url, credential);
  account_url = redact(account_url, credential);
  quota_reset_at = redact(quota_reset_at, credential);

  if (status == 401 || status == 403) {
    throw AuthError(status, code, message, request_id, retry_after,
                    upgrade_url, account_url, quota_used, quota_limit, quota_reset_at);
  }
  if (status == 429 || code == "QUOTA_EXCEEDED" || code == "RATE_LIMITED") {
    throw QuotaError(status, code, message, request_id, retry_after,
                     upgrade_url, account_url, quota_used, quota_limit, quota_reset_at);
  }
  if (status == 502 || status == 503 || status == 504 ||
      code.find("DELIVERY") != std::string::npos) {
    throw DeliveryError(status, code, message, request_id, retry_after,
                        upgrade_url, account_url, quota_used, quota_limit, quota_reset_at);
  }
  if (status == 400 || status == 404 || status == 409 ||
      status == 413 || status == 422) {
    throw ValidationError(status, code, message, request_id, retry_after,
                          upgrade_url, account_url, quota_used, quota_limit, quota_reset_at);
  }
  throw Error(status, code, message, request_id, retry_after,
              upgrade_url, account_url, quota_used, quota_limit, quota_reset_at);
}

const char kBase64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::vector<std::uint8_t>& bytes) {
  std::string out;
  out.reserve(((bytes.size() + 2) / 3) * 4);
  for (std::size_t i = 0; i < bytes.size(); i += 3) {
    const unsigned a = bytes[i];
    const unsigned b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
    const unsigned c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
    const unsigned triple = (a << 16) | (b << 8) | c;
    out.push_back(kBase64[(triple >> 18) & 0x3f]);
    out.push_back(kBase64[(triple >> 12) & 0x3f]);
    out.push_back(i + 1 < bytes.size() ? kBase64[(triple >> 6) & 0x3f] : '=');
    out.push_back(i + 2 < bytes.size() ? kBase64[triple & 0x3f] : '=');
  }
  return out;
}

std::vector<std::uint8_t> base64_decode(const std::string& input) {
  if (input.size() % 4 != 0) throw std::runtime_error("invalid base64");
  std::array<int, 256> table{};
  table.fill(-1);
  for (int i = 0; i < 64; ++i) table[static_cast<unsigned char>(kBase64[i])] = i;

  std::vector<std::uint8_t> out;
  out.reserve((input.size() / 4) * 3);
  for (std::size_t i = 0; i < input.size(); i += 4) {
    const int a = table[static_cast<unsigned char>(input[i])];
    const int b = table[static_cast<unsigned char>(input[i + 1])];
    const int c = input[i + 2] == '=' ? 0 : table[static_cast<unsigned char>(input[i + 2])];
    const int d = input[i + 3] == '=' ? 0 : table[static_cast<unsigned char>(input[i + 3])];
    if (a < 0 || b < 0 || c < 0 || d < 0) throw std::runtime_error("invalid base64");
    const unsigned triple = (static_cast<unsigned>(a) << 18) |
                            (static_cast<unsigned>(b) << 12) |
                            (static_cast<unsigned>(c) << 6) |
                            static_cast<unsigned>(d);
    out.push_back(static_cast<std::uint8_t>((triple >> 16) & 0xff));
    if (input[i + 2] != '=') out.push_back(static_cast<std::uint8_t>((triple >> 8) & 0xff));
    if (input[i + 3] != '=') out.push_back(static_cast<std::uint8_t>(triple & 0xff));
  }
  return out;
}

}  // namespace

Error::Error(long status, std::string code, std::string message,
             std::string request_id, std::optional<int> retry_after_seconds,
             std::string upgrade_url, std::string account_url,
             std::optional<long long> quota_used,
             std::optional<long long> quota_limit,
             std::string quota_reset_at)
    : std::runtime_error("Telegram Notifier " + code + " (" +
                         std::to_string(status) + "): " + message),
      status_(status),
      code_(std::move(code)),
      request_id_(std::move(request_id)),
      retry_after_seconds_(retry_after_seconds),
      upgrade_url_(std::move(upgrade_url)),
      account_url_(std::move(account_url)),
      quota_used_(quota_used),
      quota_limit_(quota_limit),
      quota_reset_at_(std::move(quota_reset_at)) {}

Client::Client(std::string source, std::string bearer_token, ClientOptions options)
    : source_(std::move(source)),
      bearer_token_(std::move(bearer_token)),
      options_(std::move(options)) {
  validate_source(source_);
  validate_header_value(bearer_token_, "bearer token");
  validate_origin(options_.origin);
  if (options_.timeout.count() <= 0 || options_.retries < 0) {
    throw std::invalid_argument("timeout must be positive and retries nonnegative");
  }
  while (!options_.origin.empty() && options_.origin.back() == '/') options_.origin.pop_back();
  (void)curl_global();
}

std::string Client::debug_string() const {
  return "TelegramNotifierClient(origin=" + options_.origin +
         ", source=" + source_ +
         ", credential=" + (bearer_token_.empty() ? "(none)" : "[REDACTED]") + ")";
}

nlohmann::json Client::list_inbox(std::optional<std::string> after, int limit) const {
  if (limit < 1 || limit > 500) throw std::invalid_argument("limit must be between 1 and 500");
  QueryParams query{{"limit", std::to_string(limit)}};
  if (after && !after->empty()) query["after"] = *after;
  return request("GET", "/v1/inbox", query);
}

nlohmann::json Client::notify(std::string text, std::string title,
                              std::optional<std::string> idempotency_key) const {
  nlohmann::json body{{"kind", "text"}, {"title", std::move(title)}, {"text", std::move(text)}};
  return request("POST", "/v1/notify", {}, &body, std::move(idempotency_key));
}

nlohmann::json Client::reply(std::string event_id, std::string text, std::string title,
                             std::optional<std::string> idempotency_key) const {
  validate_event_id(event_id);
  nlohmann::json body{{"event_id", std::move(event_id)}, {"kind", "text"},
                      {"title", std::move(title)}, {"text", std::move(text)}};
  return request("POST", "/v1/reply", {}, &body, std::move(idempotency_key));
}

nlohmann::json Client::reply_file(std::string event_id,
                                  const std::vector<std::uint8_t>& bytes,
                                  std::string filename,
                                  std::string content_type,
                                  bool photo,
                                  std::string caption,
                                  std::optional<std::string> idempotency_key) const {
  validate_event_id(event_id);
  validate_filename(filename);
  nlohmann::json body{
      {"event_id", std::move(event_id)},
      {"kind", photo ? "photo" : "document"},
      {"filename", std::move(filename)},
      {"content_type", content_type.empty() ? "application/octet-stream" : std::move(content_type)},
      {"content_base64", base64_encode(bytes)},
      {"text", std::move(caption)}};
  return request("POST", "/v1/reply", {}, &body, std::move(idempotency_key));
}

DownloadedFile Client::download_file(std::string event_id) const {
  validate_event_id(event_id);
  auto value = request("GET", "/v1/file", {{"event_id", std::move(event_id)}});
  if (!value.contains("content_base64") || !value["content_base64"].is_string()) {
    throw TransportError(0, "INVALID_RESPONSE",
                         "Telegram Notifier response did not contain file content.");
  }
  try {
    return DownloadedFile{
        value.value("filename", std::string{}),
        value.value("content_type", std::string{"application/octet-stream"}),
        base64_decode(value["content_base64"].get<std::string>())};
  } catch (const Error&) {
    throw;
  } catch (...) {
    throw TransportError(0, "INVALID_RESPONSE",
                         "Telegram Notifier returned invalid base64 file content.");
  }
}

nlohmann::json Client::request(std::string method, std::string path,
                               const QueryParams& query,
                               const nlohmann::json* body,
                               std::optional<std::string> idempotency_key) const {
  std::transform(method.begin(), method.end(), method.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  validate_public_route(method, path);

  if (method == "POST") {
    if (!idempotency_key) idempotency_key = make_idempotency_key();
    if (idempotency_key->size() < 8 || idempotency_key->size() > 200) {
      throw std::invalid_argument("idempotency key must be between 8 and 200 characters");
    }
    validate_header_value(*idempotency_key, "idempotency key");
  } else if (idempotency_key) {
    throw std::invalid_argument("idempotency key is only valid for writes");
  }

  return perform(std::move(method), std::move(path), query,
                 body ? body->dump() : std::string{},
                 std::move(idempotency_key));
}

nlohmann::json Client::perform(std::string method, std::string path,
                               const QueryParams& query, std::string body,
                               std::optional<std::string> idempotency_key) const {
  const int attempts = options_.retries + 1;

  for (int attempt = 0; attempt < attempts; ++attempt) {
    CURL* curl = curl_easy_init();
    if (!curl) {
      throw TransportError(0, "NETWORK_ERROR", "could not create HTTP transport");
    }

    std::string response_body;
    std::string url = options_.origin + path;
    if (!query.empty()) {
      url += "?";
      bool first = true;
      for (const auto& [key, value] : query) {
        if (!first) url += "&";
        first = false;
        url += escape(curl, key) + "=" + escape(curl, value);
      }
    }

    struct curl_slist* headers = nullptr;
    const std::string source_header = "X-Notifier-Source: " + source_;
    const std::string accept = "Accept: application/json";
    const std::string user_agent =
        "User-Agent: agentforge-telegram-notifier-cpp/" + std::string(kPackageVersion);
    headers = curl_slist_append(headers, source_header.c_str());
    headers = curl_slist_append(headers, accept.c_str());
    headers = curl_slist_append(headers, user_agent.c_str());

    if (!bearer_token_.empty()) {
      const std::string auth = "Authorization: Bearer " + bearer_token_;
      headers = curl_slist_append(headers, auth.c_str());
    }
    if (method == "POST") {
      headers = curl_slist_append(headers, "Content-Type: application/json");
    }
    if (idempotency_key) {
      const std::string key = "Idempotency-Key: " + *idempotency_key;
      headers = curl_slist_append(headers, key.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(options_.timeout.count()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    if (method == "POST") {
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    const auto curl_code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (curl_code != CURLE_OK) {
      if (attempt + 1 == attempts) {
        throw TransportError(0, "NETWORK_ERROR", "Telegram Notifier network request failed.");
      }
    } else if (status >= 200 && status < 300) {
      try {
        return response_body.empty() ? nlohmann::json::object()
                                     : nlohmann::json::parse(response_body);
      } catch (...) {
        throw TransportError(0, "INVALID_RESPONSE",
                             "Telegram Notifier returned invalid JSON.");
      }
    } else {
      try {
        throw_error(status, response_body, bearer_token_);
      } catch (const Error& error) {
        const bool can_retry = attempt + 1 < attempts &&
                               retryable(status) &&
                               error.code() != "QUOTA_EXCEEDED";
        if (!can_retry) throw;
      }
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(std::min(100 * (1 << std::min(attempt, 3)), 1000)));
  }

  throw TransportError(0, "NETWORK_ERROR", "Telegram Notifier request failed.");
}

}  // namespace agentforge::telegram_notifier
