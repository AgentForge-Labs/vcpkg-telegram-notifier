#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace agentforge::telegram_notifier {

inline constexpr std::string_view kDefaultOrigin = "http://127.0.0.1:8818";
inline constexpr std::string_view kPackageVersion = "0.2.0";

using QueryParams = std::map<std::string, std::string>;

struct ClientOptions {
  std::string origin{std::string(kDefaultOrigin)};
  std::chrono::milliseconds timeout{15000};
  int retries{1};
};

class Error : public std::runtime_error {
 public:
  Error(long status, std::string code, std::string message,
        std::string request_id = {},
        std::optional<int> retry_after_seconds = std::nullopt,
        std::string upgrade_url = {},
        std::string account_url = {},
        std::optional<long long> quota_used = std::nullopt,
        std::optional<long long> quota_limit = std::nullopt,
        std::string quota_reset_at = {});

  long status() const noexcept { return status_; }
  const std::string& code() const noexcept { return code_; }
  const std::string& request_id() const noexcept { return request_id_; }
  const std::optional<int>& retry_after_seconds() const noexcept { return retry_after_seconds_; }
  const std::string& upgrade_url() const noexcept { return upgrade_url_; }
  const std::string& account_url() const noexcept { return account_url_; }
  const std::optional<long long>& quota_used() const noexcept { return quota_used_; }
  const std::optional<long long>& quota_limit() const noexcept { return quota_limit_; }
  const std::string& quota_reset_at() const noexcept { return quota_reset_at_; }

 private:
  long status_;
  std::string code_;
  std::string request_id_;
  std::optional<int> retry_after_seconds_;
  std::string upgrade_url_;
  std::string account_url_;
  std::optional<long long> quota_used_;
  std::optional<long long> quota_limit_;
  std::string quota_reset_at_;
};

class AuthError final : public Error { public: using Error::Error; };
class QuotaError final : public Error { public: using Error::Error; };
class DeliveryError final : public Error { public: using Error::Error; };
class ValidationError final : public Error { public: using Error::Error; };
class TransportError final : public Error { public: using Error::Error; };

struct DownloadedFile {
  std::string filename;
  std::string content_type;
  std::vector<std::uint8_t> bytes;
};

class Client {
 public:
  explicit Client(std::string source,
                  std::string bearer_token = {},
                  ClientOptions options = {});

  const std::string& source() const noexcept { return source_; }
  const std::string& origin() const noexcept { return options_.origin; }
  std::string debug_string() const;

  nlohmann::json list_inbox(
      std::optional<std::string> after = std::nullopt,
      int limit = 50) const;

  nlohmann::json notify(
      std::string text,
      std::string title = {},
      std::optional<std::string> idempotency_key = std::nullopt) const;

  nlohmann::json reply(
      std::string event_id,
      std::string text,
      std::string title = {},
      std::optional<std::string> idempotency_key = std::nullopt) const;

  nlohmann::json reply_file(
      std::string event_id,
      const std::vector<std::uint8_t>& bytes,
      std::string filename,
      std::string content_type = "application/octet-stream",
      bool photo = false,
      std::string caption = {},
      std::optional<std::string> idempotency_key = std::nullopt) const;

  DownloadedFile download_file(std::string event_id) const;

  nlohmann::json request(
      std::string method,
      std::string path,
      const QueryParams& query = {},
      const nlohmann::json* body = nullptr,
      std::optional<std::string> idempotency_key = std::nullopt) const;

 private:
  nlohmann::json perform(
      std::string method,
      std::string path,
      const QueryParams& query,
      std::string body,
      std::optional<std::string> idempotency_key) const;

  std::string source_;
  std::string bearer_token_;
  ClientOptions options_;
};

}  // namespace agentforge::telegram_notifier
