// Session 61 AAP-v1 synthetic-only verifier. Production has no launch or
// reservation path; synthetic reservation code is compiled only for tests.
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <signal.h>
#include <poll.h>
#include <ctime>
#include <thread>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

namespace {

// RFC 8032 public test root. Its matching private seed exists only in
// temporary test fixtures; neither this key nor this binary is operationally trusted.
constexpr std::size_t kManifestMax = 128 * 1024;
constexpr std::size_t kComponentMax = 1024 * 1024;
constexpr std::size_t kTotalMax = 8 * 1024 * 1024;
constexpr std::size_t kJsonMaxDepth = 32;
// The bounded ledger admits 4096 records. Each row consumes roughly ten
// parser nodes, so retain a fixed ceiling with room for the maximum ledger.
constexpr std::size_t kJsonMaxNodes = 65536;
constexpr std::size_t kDirectoryEntryMax = 17;
constexpr std::size_t kDirectoryNameMax = 255;
constexpr std::string_view kCandidateId = "alpha-m1-collector-v2-core";
constexpr std::string_view kAuthVersion = "alpha-m1-authz-v2";
constexpr std::string_view kManifestName = "candidate_identity.json";
constexpr std::string_view kPackagePath = "qualification/alpha_m1_collector_v2";
constexpr std::string_view kEntrypointPath = "qualification/alpha_m1_collector_v2/synthetic_entry.py";
constexpr std::string_view kGateDirectory = ".alpha_m1_native_gate";
constexpr std::string_view kAAPProtocol = "AAP-v1";
constexpr std::string_view kAAPAlgorithm = "Ed25519";
constexpr std::string_view kLedgerIdentity = "alpha-m1-synthetic-ledger-v1";
constexpr std::int64_t kMaxAuthorizationTimestamp = 4102444800;  // 2100-01-01 UTC
constexpr std::string_view kTrustStoreName = "trust-store.json";
constexpr std::string_view kCandidateApprovalName = "candidate-approval.json";
constexpr std::string_view kReadsetApprovalName = "readset-approval.json";
constexpr std::string_view kExecutionApprovalName = "execution-approval.json";
constexpr std::string_view kTargetAttestationName = "target-attestation.json";
constexpr std::string_view kLedgerDirectory = "nonce-ledger";
constexpr std::string_view kLedgerName = "nonce-ledger.json";
constexpr std::string_view kLedgerLockName = "nonce-ledger.lock";
#ifdef SESSION62_TESTING
#ifndef SESSION62_PYTHON_EXECUTABLE
#error "SESSION62_TESTING requires a build-time fixed Python interpreter path"
#endif
constexpr std::string_view kSyntheticPython = SESSION62_PYTHON_EXECUTABLE;
#endif

// RFC 8032 test-vector public key. Its private seed is deliberately present
// only in synthetic tests, never in this native candidate.
constexpr std::array<unsigned char, 32> kPinnedSyntheticRootPublic = {
    0xd7,0x5a,0x98,0x01,0x82,0xb1,0x0a,0xb7,0xd5,0x4b,0xfe,0xd3,0xc9,0x64,0x07,0x3a,
    0x0e,0xe1,0x72,0xf3,0xda,0xa6,0x23,0x25,0xaf,0x02,0x1a,0x68,0xf7,0x07,0x51,0x1a};

const std::vector<std::string> kFiles = {
    "qualification/alpha_m1_collector_v2/__init__.py",
    "qualification/alpha_m1_collector_v2/aap_v1.py",
    "qualification/alpha_m1_collector_v2/adapters.py",
    "qualification/alpha_m1_collector_v2/authorization.py",
    "qualification/alpha_m1_collector_v2/budget.py",
    "qualification/alpha_m1_collector_v2/candidate_identity.py",
    "qualification/alpha_m1_collector_v2/contracts.py",
    "qualification/alpha_m1_collector_v2/nonce_ledger.py",
    "qualification/alpha_m1_collector_v2/orchestrator.py",
    "qualification/alpha_m1_collector_v2/results.py",
    "qualification/alpha_m1_collector_v2/safe_fs.py",
    "qualification/alpha_m1_collector_v2/synthetic_entry.py",
    "qualification/alpha_m1_collector_v2/target_attestation.py",
    "qualification/alpha_m1_collector_v2/trust_chain.py",
    "qualification/alpha_m1_collector_v2/verified_launcher.py",
};
const std::map<std::string, std::int64_t> kBudgets = {
    {"bytes_read", 603979776}, {"bytes_persisted", 4194304}, {"files", 256},
    {"records", 8192}, {"processes", 4096}, {"units", 32},
    {"duration_seconds", 180}, {"bytes_per_file", 134217728},
    {"bytes_per_source", 268435456}, {"output_file_bytes", 65536},
    {"manifest_bytes", 65536}, {"seal_bytes", 4096},
};

struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };

struct GateFailure : Failure {
  GateFailure(std::string gate_status, std::string message)
      : Failure(std::move(message)), status(std::move(gate_status)) {}
  std::string status;
};

class Fd {
 public:
  explicit Fd(int value = -1) : value_(value) {}
  ~Fd() { if (value_ >= 0) ::close(value_); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  Fd(Fd&& other) noexcept : value_(std::exchange(other.value_, -1)) {}
  Fd& operator=(Fd&& other) noexcept {
    if (this != &other) { if (value_ >= 0) ::close(value_); value_ = std::exchange(other.value_, -1); }
    return *this;
  }
  int get() const { return value_; }
 private:
  int value_;
};

struct Json {
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;
  std::variant<std::nullptr_t, bool, std::int64_t, std::string, Array, Object> value;
  const Object& object() const {
    if (!std::holds_alternative<Object>(value)) throw Failure("manifest field is not an object");
    return std::get<Object>(value);
  }
  const Array& array() const {
    if (!std::holds_alternative<Array>(value)) throw Failure("manifest field is not an array");
    return std::get<Array>(value);
  }
  const std::string& string() const {
    if (!std::holds_alternative<std::string>(value)) throw Failure("manifest field is not a string");
    return std::get<std::string>(value);
  }
  std::int64_t integer() const {
    if (!std::holds_alternative<std::int64_t>(value)) throw Failure("manifest field is not an integer");
    return std::get<std::int64_t>(value);
  }
};

class JsonParser {
 public:
  explicit JsonParser(std::string_view input) : input_(input) {}
  Json parse() {
    skip(); Json result = parse_value(0); skip();
    if (position_ != input_.size()) throw Failure("trailing bytes in identity manifest");
    return result;
  }
 private:
  void skip() { while (position_ < input_.size() && (input_[position_] == ' ' || input_[position_] == '\n' || input_[position_] == '\r' || input_[position_] == '\t')) ++position_; }
  char take() { if (position_ >= input_.size()) throw Failure("short JSON input"); return input_[position_++]; }
  bool consume(char c) { if (position_ < input_.size() && input_[position_] == c) { ++position_; return true; } return false; }
  std::string parse_string() {
    if (take() != '"') throw Failure("expected JSON string");
    std::string out;
    while (true) {
      const unsigned char c = static_cast<unsigned char>(take());
      if (c == '"') break;
      if (c < 0x20 || c > 0x7e) throw Failure("manifest strings must be printable ASCII");
      if (c == '\\') {
        const char escaped = take();
        if (escaped == '"' || escaped == '\\' || escaped == '/') out.push_back(escaped);
        else throw Failure("unsupported JSON escape in fixed manifest");
      } else out.push_back(static_cast<char>(c));
    }
    return out;
  }
  Json parse_value(std::size_t depth) {
    if (depth > kJsonMaxDepth || ++nodes_ > kJsonMaxNodes) throw Failure("manifest JSON complexity exceeds fixed limits");
    skip();
    if (position_ >= input_.size()) throw Failure("missing JSON value");
    if (input_[position_] == '"') return Json{parse_string()};
    if (consume('{')) {
      Json::Object result; skip();
      if (consume('}')) return Json{std::move(result)};
      while (true) {
        skip(); std::string key = parse_string(); skip();
        if (!consume(':')) throw Failure("expected colon in JSON object");
        Json value = parse_value(depth + 1);
        if (!result.emplace(std::move(key), std::move(value)).second) throw Failure("duplicate JSON key");
        skip(); if (consume('}')) break; if (!consume(',')) throw Failure("expected comma in JSON object");
      }
      return Json{std::move(result)};
    }
    if (consume('[')) {
      Json::Array result; skip();
      if (consume(']')) return Json{std::move(result)};
      while (true) { result.push_back(parse_value(depth + 1)); skip(); if (consume(']')) break; if (!consume(',')) throw Failure("expected comma in JSON array"); }
      return Json{std::move(result)};
    }
    if (input_.substr(position_, 4) == "true") { position_ += 4; return Json{true}; }
    if (input_.substr(position_, 5) == "false") { position_ += 5; return Json{false}; }
    if (input_.substr(position_, 4) == "null") { position_ += 4; return Json{nullptr}; }
    const std::size_t start = position_;
    if (consume('-')) {}
    if (position_ >= input_.size() || input_[position_] < '0' || input_[position_] > '9') throw Failure("unsupported JSON scalar");
    if (input_[position_] == '0') ++position_;
    else while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
    if (position_ < input_.size() && (input_[position_] == '.' || input_[position_] == 'e' || input_[position_] == 'E')) throw Failure("non-integer manifest number rejected");
    std::int64_t number = 0;
    try { number = std::stoll(std::string(input_.substr(start, position_ - start))); }
    catch (...) { throw Failure("manifest integer is out of range"); }
    return Json{number};
  }
  std::string_view input_;
  std::size_t position_ = 0;
  std::size_t nodes_ = 0;
};

const Json& field(const Json::Object& object, const std::string& key) {
  const auto found = object.find(key);
  if (found == object.end()) throw Failure("missing manifest field: " + key);
  return found->second;
}

void append_json_string(std::string& out, std::string_view value) {
  out.push_back('"');
  for (char raw : value) {
    const auto c = static_cast<unsigned char>(raw);
    if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(static_cast<char>(c)); }
    else if (c < 0x20 || c > 0x7e) {
      // The signed native wire schema permits printable ASCII only.
      throw Failure("non-printable or non-ASCII text is not canonical in native authorization JSON");
    } else out.push_back(static_cast<char>(c));
  }
  out.push_back('"');
}

void append_canonical(std::string& out, const Json& value) {
  if (std::holds_alternative<std::nullptr_t>(value.value)) { out += "null"; return; }
  if (std::holds_alternative<bool>(value.value)) { out += std::get<bool>(value.value) ? "true" : "false"; return; }
  if (std::holds_alternative<std::int64_t>(value.value)) { out += std::to_string(std::get<std::int64_t>(value.value)); return; }
  if (std::holds_alternative<std::string>(value.value)) { append_json_string(out, std::get<std::string>(value.value)); return; }
  if (std::holds_alternative<Json::Array>(value.value)) {
    out.push_back('['); bool first = true;
    for (const auto& item : std::get<Json::Array>(value.value)) { if (!first) out.push_back(','); first = false; append_canonical(out, item); }
    out.push_back(']'); return;
  }
  out.push_back('{'); bool first = true;
  for (const auto& [key, item] : std::get<Json::Object>(value.value)) {
    if (!first) out.push_back(',');
    first = false;
    append_json_string(out, key); out.push_back(':'); append_canonical(out, item);
  }
  out.push_back('}');
}

std::string canonical_json(const Json& value) {
  std::string result;
  append_canonical(result, value);
  return result;
}

std::vector<unsigned char> decode_hex(std::string_view value, std::size_t expected_bytes) {
  if (value.size() != expected_bytes * 2) throw Failure("hex field has an invalid length");
  auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  std::vector<unsigned char> result(expected_bytes);
  for (std::size_t i = 0; i < expected_bytes; ++i) {
    const int high = digit(value[2 * i]), low = digit(value[2 * i + 1]);
    if (high < 0 || low < 0) throw Failure("hex field must be lowercase hexadecimal");
    result[i] = static_cast<unsigned char>((high << 4) | low);
  }
  return result;
}

std::string aap_signing_bytes(std::string_view kind, std::string_view canonical_payload) {
  static constexpr char domain_bytes[] = "MONAD-AAP-v1\0";
  static constexpr std::string_view domain(domain_bytes, sizeof(domain_bytes) - 1);
  std::string result(domain.data(), domain.size());
  result.append(kind);
  result.push_back('\0');
  result.append(canonical_payload);
  return result;
}

bool ed25519_verify(const unsigned char* public_key, std::string_view kind,
                    std::string_view canonical_payload, std::string_view signature_hex) {
  const auto signature = decode_hex(signature_hex, 64);
  const std::string message = aap_signing_bytes(kind, canonical_payload);
  EVP_PKEY* raw_key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, public_key, 32);
  if (!raw_key) throw Failure("cannot initialize Ed25519 public key");
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(raw_key, EVP_PKEY_free);
  EVP_MD_CTX* raw_context = EVP_MD_CTX_new();
  if (!raw_context) throw Failure("cannot initialize Ed25519 verify context");
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(raw_context, EVP_MD_CTX_free);
  if (EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1) throw Failure("cannot configure Ed25519 verifier");
  return EVP_DigestVerify(context.get(), signature.data(), signature.size(),
                          reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1;
}

void exact_keys(const Json::Object& object, std::set<std::string> expected, std::string_view label) {
  std::set<std::string> actual;
  for (const auto& [key, _] : object) actual.insert(key);
  if (actual != expected) throw Failure(std::string(label) + " has unexpected or missing fields");
}

std::string hex(const unsigned char* bytes, std::size_t length) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result(length * 2, '0');
  for (std::size_t i = 0; i < length; ++i) { result[2 * i] = digits[bytes[i] >> 4]; result[2 * i + 1] = digits[bytes[i] & 15]; }
  return result;
}

std::vector<unsigned char> sha256(std::string_view bytes) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int length = 0;
  if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &length, EVP_sha256(), nullptr) != 1 || length != 32) throw Failure("SHA-256 provider failed");
  return {digest.begin(), digest.begin() + length};
}

struct OpenedFile { std::string bytes; std::string digest; };

void check_stable(const struct stat& before, const struct stat& after) {
  if (before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
      before.st_mode != after.st_mode || before.st_nlink != after.st_nlink ||
      before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
      before.st_mtim.tv_nsec != after.st_mtim.tv_nsec || before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
      before.st_ctim.tv_nsec != after.st_ctim.tv_nsec) throw Failure("file changed during verification");
}

OpenedFile read_regular_at(int directory_fd, std::string_view name, std::size_t maximum,
                           bool require_private = false) {
  const std::string leaf(name);
  Fd fd(::openat(directory_fd, leaf.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  if (fd.get() < 0) throw Failure("cannot open candidate file without following links: " + leaf);
  struct stat before{};
  if (::fstat(fd.get(), &before) != 0) throw Failure("cannot stat opened candidate file");
  if (!S_ISREG(before.st_mode) || before.st_nlink != 1 || before.st_uid != ::geteuid() ||
      before.st_size < 0 || static_cast<std::uint64_t>(before.st_size) > maximum ||
      (require_private && (before.st_mode & 0077) != 0)) throw Failure("candidate file type, owner, link count, mode, or size rejected: " + leaf);
  std::string bytes;
  bytes.reserve(static_cast<std::size_t>(before.st_size));
  while (bytes.size() < static_cast<std::size_t>(before.st_size)) {
    char buffer[65536];
    const auto remaining = static_cast<std::size_t>(before.st_size) - bytes.size();
    const ssize_t count = ::read(fd.get(), buffer, std::min(sizeof(buffer), remaining));
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) throw Failure("short read while checking candidate file: " + leaf);
    bytes.append(buffer, static_cast<std::size_t>(count));
#ifdef SESSION62_TESTING
    const char* fault = std::getenv("M1_SESSION62_FAULT");
    if (leaf == "adapters.py" && bytes.size() > 0 && fault &&
        std::string_view(fault) == "truncate-during-read") {
      Fd mutator(::openat(directory_fd, leaf.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW));
      if (mutator.get() < 0 || ::ftruncate(mutator.get(), 1) != 0)
        throw Failure("test concurrent truncate injection failed");
    }
#endif
  }
#ifdef SESSION62_TESTING
  const char* read_fault = std::getenv("M1_SESSION62_FAULT");
  if (leaf == "adapters.py" && read_fault && std::string_view(read_fault) == "mutate-during-read") {
    Fd mutator(::openat(directory_fd, leaf.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW));
    const char changed = '#';
    if (mutator.get() < 0 || ::pwrite(mutator.get(), &changed, 1, 0) != 1)
      throw Failure("test concurrent mutation injection failed");
  }
#endif
  char extra;
  ssize_t extra_count;
  do { extra_count = ::read(fd.get(), &extra, 1); } while (extra_count < 0 && errno == EINTR);
  if (extra_count != 0) throw Failure("candidate file grew during bounded read: " + leaf);
  struct stat after{};
  if (::fstat(fd.get(), &after) != 0) throw Failure("cannot restat candidate file");
  check_stable(before, after);
  return {std::move(bytes), {}};
}

Fd open_absolute_directory(std::string_view path) {
  if (path.empty() || path.front() != '/' || path.back() == '/') throw Failure("candidate root must be canonical absolute path");
  Fd current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (current.get() < 0) throw Failure("cannot open filesystem root");
  std::size_t pos = 1;
  while (pos < path.size()) {
    const auto end = path.find('/', pos);
    const auto part = path.substr(pos, end == std::string_view::npos ? path.size() - pos : end - pos);
    if (part.empty() || part == "." || part == "..") throw Failure("candidate root path is not canonical");
    const std::string component(part);
    Fd next(::openat(current.get(), component.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (next.get() < 0) throw Failure("candidate root contains an unsafe path component");
    current = std::move(next);
    if (end == std::string_view::npos) break;
    pos = end + 1;
  }
  return current;
}

Fd open_package_dir(int root_fd) {
  Fd qualification(::openat(root_fd, "qualification", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (qualification.get() < 0) throw Failure("candidate qualification directory missing or unsafe");
  Fd package(::openat(qualification.get(), "alpha_m1_collector_v2", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (package.get() < 0) throw Failure("candidate v2 package directory missing or unsafe");
  return package;
}

std::set<std::string> directory_names(int package_fd) {
  const int duplicate = ::dup(package_fd);
  if (duplicate < 0) throw Failure("cannot duplicate package directory descriptor");
  if (::lseek(duplicate, 0, SEEK_SET) < 0) { ::close(duplicate); throw Failure("cannot rewind package directory descriptor"); }
  DIR* directory = ::fdopendir(duplicate);
  if (!directory) { ::close(duplicate); throw Failure("cannot enumerate candidate package"); }
  std::set<std::string> names;
  errno = 0;
  while (dirent* entry = ::readdir(directory)) {
    const std::string name(entry->d_name);
    if (name != "." && name != "..") {
      if (name.size() > kDirectoryNameMax || names.size() >= kDirectoryEntryMax) {
        errno = EOVERFLOW;
        break;
      }
      names.insert(name);
    }
    errno = 0;
  }
  const int read_error = errno;
  ::closedir(directory);
  if (read_error) throw Failure("candidate package enumeration failed");
  return names;
}

std::set<std::string> expected_directory_names() {
  std::set<std::string> result = {std::string(kManifestName), "native"};
  for (const auto& path : kFiles) result.insert(path.substr(kPackagePath.size() + 1));
  return result;
}

struct Candidate {
  std::string manifest_raw;
  std::string manifest_sha256;
  std::string aggregate_sha256;
  Json manifest;
  std::map<std::string, std::string> components;
};

Candidate verify_manifest(int root_fd) {
  Fd package = open_package_dir(root_fd);
  struct stat package_before{};
  if (::fstat(package.get(), &package_before) != 0 || !S_ISDIR(package_before.st_mode)) throw Failure("candidate package descriptor is not a directory");
  const auto names_before = directory_names(package.get());
  if (names_before != expected_directory_names()) throw Failure("candidate package inventory contains missing or extra entries");
  Fd native_dir(::openat(package.get(), "native", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (native_dir.get() < 0) throw Failure("native candidate source directory is missing or unsafe");
  auto manifest_file = read_regular_at(package.get(), kManifestName, kManifestMax);
  const Json manifest = JsonParser(manifest_file.bytes).parse();
  if (canonical_json(manifest) != manifest_file.bytes) throw GateFailure("POLICY_ERROR", "candidate manifest is not canonical JSON");
  const auto& object = manifest.object();
  exact_keys(object, {"schema_version", "candidate_id", "authorization_contract_version", "components", "aggregate_sha256", "entrypoint", "read_set_maximum", "budget_maximum"}, "identity manifest");
  if (field(object, "schema_version").integer() != 1 || field(object, "candidate_id").string() != kCandidateId || field(object, "authorization_contract_version").string() != kAuthVersion) throw Failure("candidate identity contract mismatch");
  const auto entry = field(object, "entrypoint").object();
  exact_keys(entry, {"path", "callable"}, "entrypoint");
  if (field(entry, "path").string() != kEntrypointPath || field(entry, "callable").string() != "main") throw Failure("entry point is not the fixed synthetic function");
  const auto modules = field(object, "read_set_maximum").array();
  const std::vector<std::string> expected_modules = {"identity", "systemd-state", "process-cgroups"};
  if (modules.size() != expected_modules.size()) throw Failure("read set maximum differs from code policy");
  for (std::size_t i = 0; i < modules.size(); ++i) if (modules[i].string() != expected_modules[i]) throw Failure("read set maximum differs from code policy");
  const auto budget = field(object, "budget_maximum").object();
  if (budget.size() != kBudgets.size()) throw Failure("budget schema differs from code policy");
  for (const auto& [name, maximum] : kBudgets) if (field(budget, name).integer() != maximum) throw Failure("budget maximum differs from code policy");
  const auto components = field(object, "components").array();
  if (components.size() != kFiles.size()) throw Failure("candidate component count mismatch");

  EVP_MD_CTX* raw_context = EVP_MD_CTX_new();
  if (!raw_context) throw Failure("cannot allocate aggregate hash context");
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(raw_context, EVP_MD_CTX_free);
  if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) throw Failure("cannot initialize aggregate SHA-256");
  static constexpr char prefix[] = "monad-alpha-m1-candidate-v1";
  if (EVP_DigestUpdate(context.get(), prefix, sizeof(prefix)) != 1) throw Failure("cannot update aggregate hash");
  std::uint64_t total_bytes = 0;
  std::map<std::string, std::string> captured_components;
  const int package_fd = package.get();
  for (std::size_t i = 0; i < kFiles.size(); ++i) {
    const auto& row = components[i].object();
    exact_keys(row, {"path", "bytes", "sha256"}, "component row");
    const std::string path = field(row, "path").string();
    if (path != kFiles[i]) throw Failure("candidate component path/order mismatch");
    const auto expected_size = field(row, "bytes").integer();
    const std::string expected_digest = field(row, "sha256").string();
    const std::string basename = path.substr(kPackagePath.size() + 1);
    const std::size_t remaining_component_budget = kTotalMax - static_cast<std::size_t>(total_bytes);
    auto opened = read_regular_at(package_fd, basename, std::min(kComponentMax, remaining_component_budget));
    if (opened.bytes.empty() || opened.bytes.size() != static_cast<std::uint64_t>(expected_size)) throw Failure("candidate component size mismatch: " + basename);
    const auto digest = sha256(opened.bytes);
    const std::string actual_digest = hex(digest.data(), digest.size());
    if (actual_digest != expected_digest) throw Failure("candidate component digest mismatch: " + basename);
    total_bytes += opened.bytes.size();
    if (total_bytes > kTotalMax) throw Failure("candidate total byte cap exceeded");
    const std::uint32_t path_len = htonl(static_cast<std::uint32_t>(path.size()));
    const std::uint64_t host_byte_len = opened.bytes.size();
    std::array<unsigned char, 8> byte_len{};
    for (std::size_t j = 0; j < byte_len.size(); ++j) byte_len[j] = static_cast<unsigned char>((host_byte_len >> (8 * (7 - j))) & 0xff);
    if (EVP_DigestUpdate(context.get(), &path_len, sizeof(path_len)) != 1 ||
        EVP_DigestUpdate(context.get(), path.data(), path.size()) != 1 ||
        EVP_DigestUpdate(context.get(), byte_len.data(), byte_len.size()) != 1 ||
        EVP_DigestUpdate(context.get(), digest.data(), digest.size()) != 1) throw Failure("aggregate hash update failed");
    captured_components.emplace(path, std::move(opened.bytes));
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> aggregate{};
  unsigned int aggregate_len = 0;
  if (EVP_DigestFinal_ex(context.get(), aggregate.data(), &aggregate_len) != 1 || aggregate_len != 32) throw Failure("aggregate digest finalization failed");
  const std::string actual_aggregate = hex(aggregate.data(), aggregate_len);
  const std::string expected_aggregate = field(object, "aggregate_sha256").string();
  if (actual_aggregate != expected_aggregate) throw Failure("candidate aggregate digest mismatch");
  const auto names_after = directory_names(package.get());
  struct stat package_after{};
  if (::fstat(package.get(), &package_after) != 0 || names_after != names_before ||
      package_before.st_dev != package_after.st_dev || package_before.st_ino != package_after.st_ino ||
      package_before.st_mode != package_after.st_mode || package_before.st_nlink != package_after.st_nlink ||
      package_before.st_mtim.tv_sec != package_after.st_mtim.tv_sec ||
      package_before.st_mtim.tv_nsec != package_after.st_mtim.tv_nsec ||
      package_before.st_ctim.tv_sec != package_after.st_ctim.tv_sec ||
      package_before.st_ctim.tv_nsec != package_after.st_ctim.tv_nsec) throw Failure("candidate package changed during verification");
  return {manifest_file.bytes, hex(sha256(manifest_file.bytes).data(), 32), actual_aggregate,
          manifest, std::move(captured_components)};
}

struct TrustedKey {
  std::string key_id;
  std::string principal_id;
  std::string role;
  std::array<unsigned char, 32> public_key{};
  std::int64_t not_before = 0;
  std::int64_t not_after = 0;
};

using KeyMap = std::map<std::string, TrustedKey>;

struct Approval {
  std::string key_id, principal_id, kind, nonce, candidate_sha256, target_id, signature;
  std::int64_t issued_at = 0, expires_at = 0;
  std::vector<std::string> modules;
  std::map<std::string, std::int64_t> budgets;
};

struct GateResult {
  std::string candidate_sha256;
  std::string target_id;
  std::string nonce_state = "AVAILABLE";
  std::string nonce;
  std::string authorization_sha256;
  std::string reservation_id;
};

Json read_canonical_document(int directory_fd, std::string_view name, std::size_t maximum,
                             std::string* raw_out = nullptr, bool private_file = true) {
  OpenedFile file;
  try { file = read_regular_at(directory_fd, name, maximum, private_file); }
  catch (const std::exception& error) { throw GateFailure("DENIED", std::string("required authorization document unavailable: ") + error.what()); }
  Json document = JsonParser(file.bytes).parse();
  if (canonical_json(document) != file.bytes) throw GateFailure("POLICY_ERROR", "native authorization document is not canonical JSON");
  if (raw_out) *raw_out = file.bytes;
  return document;
}

Json without_field(const Json& document, std::string_view name) {
  Json::Object object = document.object();
  if (object.erase(std::string(name)) != 1) throw Failure("signed document has no signature field");
  return Json{std::move(object)};
}

std::string json_string(const Json::Object& object, const std::string& name) {
  return field(object, name).string();
}

bool safe_token(std::string_view value, std::size_t max);

std::int64_t json_integer(const Json::Object& object, const std::string& name) {
  return field(object, name).integer();
}

Fd open_gate_directory(int root_fd) {
  Fd gate(::openat(root_fd, std::string(kGateDirectory).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (gate.get() < 0) throw GateFailure("DENIED", "native authorization fixture is missing or unsafe");
  struct stat info{};
  if (::fstat(gate.get(), &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != ::geteuid() ||
      (info.st_mode & 0077) != 0) throw GateFailure("POLICY_ERROR", "native authorization directory must be owner-only");
  const std::set<std::string> expected = {
      std::string(kTrustStoreName), std::string(kCandidateApprovalName),
      std::string(kReadsetApprovalName), std::string(kExecutionApprovalName),
      std::string(kTargetAttestationName), std::string(kLedgerDirectory)};
  if (directory_names(gate.get()) != expected) throw GateFailure("POLICY_ERROR", "authorization directory inventory mismatch");
  return gate;
}

KeyMap load_trust_store(int gate_fd, std::int64_t now) {
  const Json envelope = read_canonical_document(gate_fd, kTrustStoreName, 128 * 1024);
  const auto& object = envelope.object();
  exact_keys(object, {"algorithm", "keys", "kind", "protocol_version", "schema_version", "signature"}, "trust store");
  if (json_integer(object, "schema_version") != 1 || json_string(object, "protocol_version") != kAAPProtocol ||
      json_string(object, "algorithm") != kAAPAlgorithm || json_string(object, "kind") != "trust-store")
    throw GateFailure("POLICY_ERROR", "native trust-store schema or policy mismatch");
  const std::string root_signature = json_string(object, "signature");
  if (!ed25519_verify(kPinnedSyntheticRootPublic.data(), "trust-store",
                      canonical_json(without_field(envelope, "signature")), root_signature))
    throw GateFailure("DENIED", "trust store is not signed by the pinned synthetic root");
  const auto keys = field(object, "keys").array();
  const std::set<std::string> roles = {"candidate-approver", "read-set-approver", "execution-approver", "operator-target-attestor"};
  if (keys.size() != roles.size()) throw GateFailure("POLICY_ERROR", "trust store must contain exactly one key per required role");
  KeyMap result;
  std::set<std::string> seen_roles, seen_public, seen_principals;
  for (const auto& item : keys) {
    const auto& row = item.object();
    exact_keys(row, {"key_id", "principal_id", "role", "public_key_hex", "not_before", "not_after"}, "trust key");
    TrustedKey key;
    key.key_id = json_string(row, "key_id");
    key.principal_id = json_string(row, "principal_id");
    key.role = json_string(row, "role");
    key.not_before = json_integer(row, "not_before");
    key.not_after = json_integer(row, "not_after");
    const auto public_bytes = decode_hex(json_string(row, "public_key_hex"), 32);
    std::copy(public_bytes.begin(), public_bytes.end(), key.public_key.begin());
    if (!safe_token(key.key_id, 64) || !safe_token(key.principal_id, 96) ||
        !roles.contains(key.role) || key.not_before < 0 || key.not_after > kMaxAuthorizationTimestamp ||
        key.not_after <= key.not_before)
      throw GateFailure("POLICY_ERROR", "trust key identity, role, or validity is invalid");
    if (key.not_before > now || key.not_after <= now) throw GateFailure("EXPIRED", "trusted role key is outside its validity window");
    const std::string public_hex = hex(key.public_key.data(), key.public_key.size());
    if (!seen_roles.insert(key.role).second || !seen_public.insert(public_hex).second ||
        !seen_principals.insert(key.principal_id).second ||
        !result.emplace(key.key_id, key).second) throw GateFailure("POLICY_ERROR", "trust roles, key IDs, and public keys must be unique");
  }
  return result;
}

bool safe_token(std::string_view value, std::size_t max = 96) {
  if (value.empty() || value.size() > max) return false;
  for (char raw : value) {
    const auto c = static_cast<unsigned char>(raw);
    if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') &&
        !(c >= '0' && c <= '9') && c != '.' && c != '_' && c != ':' && c != '-') return false;
  }
  return (value.front() >= 'A' && value.front() <= 'Z') || (value.front() >= 'a' && value.front() <= 'z') ||
      (value.front() >= '0' && value.front() <= '9');
}

Approval verify_approval(int gate_fd, std::string_view filename, const KeyMap& keys,
                         std::string_view expected_role, std::string_view expected_kind,
                         std::string_view candidate_digest, std::int64_t now) {
  const Json envelope = read_canonical_document(gate_fd, filename, 64 * 1024);
  const auto& object = envelope.object();
  exact_keys(object, {"algorithm", "schema_version", "protocol_version", "kind", "key_id", "approver_id", "issued_at", "expires_at", "nonce", "candidate_sha256", "target_id", "modules", "budgets", "signature"}, "approval");
  if (json_integer(object, "schema_version") != 1 || json_string(object, "protocol_version") != kAAPProtocol ||
      json_string(object, "algorithm") != kAAPAlgorithm || json_string(object, "kind") != expected_kind)
    throw GateFailure("POLICY_ERROR", "approval schema, protocol, algorithm, or kind mismatch");
  Approval approval;
  approval.kind = json_string(object, "kind"); approval.key_id = json_string(object, "key_id");
  approval.principal_id = json_string(object, "approver_id"); approval.issued_at = json_integer(object, "issued_at");
  approval.expires_at = json_integer(object, "expires_at"); approval.nonce = json_string(object, "nonce");
  approval.candidate_sha256 = json_string(object, "candidate_sha256"); approval.target_id = json_string(object, "target_id");
  approval.signature = json_string(object, "signature");
  if (approval.candidate_sha256 != candidate_digest) throw GateFailure("CANDIDATE_MISMATCH", "approval candidate digest differs from measured manifest");
  if (!safe_token(approval.principal_id) || !safe_token(approval.target_id) ||
      approval.nonce.size() < 22 || approval.nonce.size() > 96 || !safe_token(approval.nonce, 96))
    throw GateFailure("POLICY_ERROR", "approval identity, target, or nonce format invalid");
  if (approval.issued_at < 0 || approval.expires_at < 0 ||
      approval.issued_at > kMaxAuthorizationTimestamp || approval.expires_at > kMaxAuthorizationTimestamp ||
      approval.issued_at > now || approval.expires_at <= now || approval.expires_at <= approval.issued_at ||
      approval.expires_at - approval.issued_at > 300)
    throw GateFailure("EXPIRED", "approval is expired or outside its 300-second validity window");
  const auto key_it = keys.find(approval.key_id);
  if (key_it == keys.end() || key_it->second.role != expected_role || key_it->second.principal_id != approval.principal_id)
    throw GateFailure("DENIED", "approval key is not trusted for its claimed principal and role");
  if (!ed25519_verify(key_it->second.public_key.data(), expected_kind,
                      canonical_json(without_field(envelope, "signature")), approval.signature))
    throw GateFailure("DENIED", "approval signature is invalid");
  const auto modules = field(object, "modules").array();
  const auto budgets_object = field(object, "budgets").object();
  for (const auto& module : modules) approval.modules.push_back(module.string());
  for (const auto& [name, value] : budgets_object) approval.budgets.emplace(name, value.integer());
  return approval;
}

void verify_scope(const Approval& approval, const Candidate& candidate, bool is_candidate_approval) {
  if (is_candidate_approval) {
    if (!approval.modules.empty() || !approval.budgets.empty()) throw GateFailure("DENIED", "candidate approval must not grant a read or execution scope");
    return;
  }
  const std::vector<std::string> modules = {"identity", "systemd-state", "process-cgroups"};
  if (approval.modules != modules) throw GateFailure("DENIED", "approval read-set scope is missing, reordered, or expanded");
  std::map<std::string, std::int64_t> budgets;
  const auto budget_maximum = field(candidate.manifest.object(), "budget_maximum").object();
  for (const auto& [key, value] : budget_maximum) budgets.emplace(key, value.integer());
  if (approval.budgets != budgets) throw GateFailure("DENIED", "approval budget differs from fixed candidate maximum");
}

void verify_identity(const Json& identity) {
  const auto& object = identity.object();
  exact_keys(object, {"target_id", "machine_id", "hostname", "boot_id", "snapshot_id"}, "target identity");
  const std::string machine_id = json_string(object, "machine_id"), boot_id = json_string(object, "boot_id");
  const bool machine_hex = machine_id.size() == 32 && std::all_of(machine_id.begin(), machine_id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
  bool boot_format = boot_id.size() == 36;
  for (std::size_t i = 0; i < boot_id.size() && boot_format; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) boot_format = boot_id[i] == '-';
    else boot_format = (boot_id[i] >= '0' && boot_id[i] <= '9') || (boot_id[i] >= 'a' && boot_id[i] <= 'f');
  }
  if (!safe_token(json_string(object, "target_id")) || !safe_token(json_string(object, "hostname")) ||
      !safe_token(json_string(object, "snapshot_id")) || !machine_hex || !boot_format)
    throw GateFailure("TARGET_MISMATCH", "synthetic target identity format is invalid");
}

std::string verify_target_attestation(int gate_fd, const KeyMap& keys, const Candidate& candidate,
                               const Approval& readset, const Approval& execution,
                               const Approval& candidate_approval, std::int64_t now) {
  const Json envelope = read_canonical_document(gate_fd, kTargetAttestationName, 64 * 1024);
  const auto& object = envelope.object();
  exact_keys(object, {"algorithm", "schema_version", "protocol_version", "kind", "key_id", "operator_id", "candidate_sha256", "request_nonce", "target_id", "declared_identity", "observed_identity", "provenance", "evidence_ref", "issued_at", "expires_at", "snapshot_ref", "signature"}, "target attestation");
  if (json_integer(object, "schema_version") != 1 || json_string(object, "protocol_version") != kAAPProtocol ||
      json_string(object, "algorithm") != kAAPAlgorithm || json_string(object, "kind") != "target-attestation")
    throw GateFailure("POLICY_ERROR", "target attestation schema or policy mismatch");
  const Json declared = field(object, "declared_identity");
  const Json observed = field(object, "observed_identity");
  verify_identity(declared); verify_identity(observed);
  const std::string target_id = json_string(object, "target_id");
  if (canonical_json(declared) != canonical_json(observed) || json_string(declared.object(), "target_id") != target_id ||
      json_string(observed.object(), "target_id") != target_id || target_id != readset.target_id ||
      target_id != execution.target_id || target_id != candidate_approval.target_id)
    throw GateFailure("TARGET_MISMATCH", "declared, observed, and authorized target identities differ");
  if (json_string(object, "candidate_sha256") != candidate.manifest_sha256 ||
      json_string(object, "request_nonce") != readset.nonce || readset.nonce != execution.nonce ||
      readset.nonce != candidate_approval.nonce)
    throw GateFailure("TARGET_MISMATCH", "target attestation is not bound to this candidate and authorization nonce");
  if (json_string(object, "provenance") != "synthetic-provider-v1" ||
      json_string(object, "snapshot_ref") != json_string(observed.object(), "snapshot_id") ||
      !safe_token(json_string(object, "evidence_ref"))) throw GateFailure("TARGET_MISMATCH", "target provenance or snapshot reference invalid");
  const auto issued_at = json_integer(object, "issued_at"), expires_at = json_integer(object, "expires_at");
  if (issued_at < 0 || expires_at < 0 ||
      issued_at > kMaxAuthorizationTimestamp || expires_at > kMaxAuthorizationTimestamp ||
      issued_at > now || expires_at <= now || expires_at <= issued_at ||
      expires_at - issued_at > 300)
    throw GateFailure("EXPIRED", "target attestation is expired or outside freshness policy");
  const std::string key_id = json_string(object, "key_id"), operator_id = json_string(object, "operator_id");
  const auto key = keys.find(key_id);
  if (key == keys.end() || key->second.role != "operator-target-attestor" || key->second.principal_id != operator_id)
    throw GateFailure("DENIED", "target attestor key/principal/role is not trusted");
  if (operator_id == readset.principal_id || operator_id == execution.principal_id || operator_id == candidate_approval.principal_id)
    throw GateFailure("DENIED", "target attestor must be independent from all approvers");
  if (!ed25519_verify(key->second.public_key.data(), "target-attestation",
                      canonical_json(without_field(envelope, "signature")), json_string(object, "signature")))
    throw GateFailure("DENIED", "target attestation signature is invalid");
  return json_string(object, "signature");
}

Fd open_ledger_directory(int gate_fd) {
  Fd ledger(::openat(gate_fd, std::string(kLedgerDirectory).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (ledger.get() < 0) throw GateFailure("INCONCLUSIVE", "nonce ledger directory is missing or unsafe");
  struct stat info{};
  if (::fstat(ledger.get(), &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != ::geteuid() || (info.st_mode & 0077) != 0)
    throw GateFailure("POLICY_ERROR", "nonce ledger directory must be owner-only");
  if (directory_names(ledger.get()) != std::set<std::string>{std::string(kLedgerName), std::string(kLedgerLockName)})
    throw GateFailure("INCONCLUSIVE", "nonce ledger directory has unexpected or partial state");
  return ledger;
}

Fd lock_ledger(int ledger_fd) {
  Fd lock(::openat(ledger_fd, std::string(kLedgerLockName).c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  if (lock.get() < 0) throw GateFailure("INCONCLUSIVE", "nonce ledger lock is missing or unsafe");
  struct stat info{};
  if (::fstat(lock.get(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
      info.st_uid != ::geteuid() || (info.st_mode & 0077) != 0 || info.st_size != 0)
    throw GateFailure("POLICY_ERROR", "nonce lock file has invalid type, owner, mode, or contents");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (::flock(lock.get(), LOCK_EX | LOCK_NB) != 0) {
    if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) throw GateFailure("INCONCLUSIVE", "nonce ledger lock failed");
    if (std::chrono::steady_clock::now() >= deadline) throw GateFailure("INCONCLUSIVE", "nonce ledger lock timed out");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return lock;
}

Json read_ledger(int ledger_fd) {
  Json document;
  try {
    document = read_canonical_document(ledger_fd, kLedgerName, 4 * 1024 * 1024);
  } catch (const GateFailure& error) {
    throw GateFailure("INCONCLUSIVE", std::string("nonce ledger could not be verified: ") + error.what());
  } catch (const std::exception& error) {
    throw GateFailure("INCONCLUSIVE", std::string("nonce ledger could not be parsed: ") + error.what());
  }
  const auto& object = document.object();
  exact_keys(object, {"schema_version", "protocol_version", "ledger_id", "records", "sha256"}, "nonce ledger");
  if (json_integer(object, "schema_version") != 2 ||
      json_string(object, "protocol_version") != kAAPProtocol ||
      json_string(object, "ledger_id") != kLedgerIdentity)
    throw GateFailure("INCONCLUSIVE", "nonce ledger identity or protocol version invalid");
  const Json unsigned_document = without_field(document, "sha256");
  const auto digest = sha256(canonical_json(unsigned_document));
  if (json_string(object, "sha256") != hex(digest.data(), digest.size())) throw GateFailure("INCONCLUSIVE", "nonce ledger checksum mismatch");
  const auto records = field(object, "records").array();
  if (records.size() > 4096) throw GateFailure("INCONCLUSIVE", "nonce ledger capacity exceeded");
  std::set<std::string> nonces;
  std::set<std::string> reservations;
  for (const auto& record : records) {
    const auto& row = record.object();
    exact_keys(row, {"authorization_sha256", "candidate_sha256", "ledger_id", "nonce",
                     "protocol_version", "reservation_id", "state", "target_id"}, "nonce ledger record");
    const auto nonce = json_string(row, "nonce"), state = json_string(row, "state");
    const auto reservation = json_string(row, "reservation_id");
    const auto candidate = json_string(row, "candidate_sha256");
    const auto authz = json_string(row, "authorization_sha256");
    if (!safe_token(nonce) || nonce.size() < 22 || reservation.size() != 36 ||
        reservation.substr(0, 4) != "res-" ||
        !std::all_of(reservation.begin() + 4, reservation.end(), [](char c) {
          return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }) ||
        !safe_token(json_string(row, "target_id")) ||
        json_string(row, "ledger_id") != kLedgerIdentity ||
        json_string(row, "protocol_version") != kAAPProtocol ||
        candidate.size() != 64 || authz.size() != 64 ||
        !std::all_of(candidate.begin(), candidate.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
        !std::all_of(authz.begin(), authz.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
        (state != "RESERVED" && state != "CONSUMED") ||
        !nonces.insert(nonce).second || !reservations.insert(reservation).second)
      throw GateFailure("INCONCLUSIVE", "nonce ledger record is malformed or duplicated");
  }
  return document;
}

std::optional<Json::Object> nonce_record(const Json& ledger, std::string_view nonce) {
  const Json records_value = field(ledger.object(), "records");
  const auto& records = records_value.array();
  for (const auto& record : records) {
    const auto& row = record.object();
    if (json_string(row, "nonce") == nonce) return row;
  }
  return std::nullopt;
}

GateResult verify_native_gate(int root_fd, const Candidate& candidate, bool allow_reserved = false,
                              bool* ledger_check_started = nullptr) {
  const std::time_t wall_now = std::time(nullptr);
  if (wall_now < 0) throw GateFailure("INCONCLUSIVE", "system clock unavailable");
  const auto now = static_cast<std::int64_t>(wall_now);
  Fd gate = open_gate_directory(root_fd);
  const KeyMap keys = load_trust_store(gate.get(), now);
  const Approval candidate_approval = verify_approval(gate.get(), kCandidateApprovalName, keys,
      "candidate-approver", "candidate-identity", candidate.manifest_sha256, now);
  const Approval readset = verify_approval(gate.get(), kReadsetApprovalName, keys,
      "read-set-approver", "read-set", candidate.manifest_sha256, now);
  const Approval execution = verify_approval(gate.get(), kExecutionApprovalName, keys,
      "execution-approver", "execution", candidate.manifest_sha256, now);
  verify_scope(candidate_approval, candidate, true);
  verify_scope(readset, candidate, false);
  verify_scope(execution, candidate, false);
  if (readset.nonce != execution.nonce || candidate_approval.nonce != readset.nonce ||
      readset.target_id != execution.target_id || readset.target_id != candidate_approval.target_id)
    throw GateFailure("DENIED", "authorization documents are not bound to one target and nonce");
  if (candidate_approval.principal_id == readset.principal_id || candidate_approval.principal_id == execution.principal_id ||
      readset.principal_id == execution.principal_id || readset.key_id == execution.key_id)
    throw GateFailure("DENIED", "candidate, read-set, and execution approvers must be distinct");
  const std::string target_signature = verify_target_attestation(gate.get(), keys, candidate, readset, execution, candidate_approval, now);
  const Json authorization_signatures{Json::Object{
      {"candidate", Json{candidate_approval.signature}}, {"readset", Json{readset.signature}},
      {"execution", Json{execution.signature}}, {"target", Json{target_signature}}}};
  const auto authz_digest_bytes = sha256(canonical_json(authorization_signatures));
  const std::string authz_digest = hex(authz_digest_bytes.data(), authz_digest_bytes.size());
  if (ledger_check_started) *ledger_check_started = true;
  Fd ledger_dir = open_ledger_directory(gate.get());
  Fd lock = lock_ledger(ledger_dir.get());
  Json ledger;
  try {
    ledger = read_ledger(ledger_dir.get());
  } catch (const GateFailure& error) {
    throw GateFailure("INCONCLUSIVE", std::string("nonce ledger state is not trustworthy: ") + error.what());
  } catch (const std::exception& error) {
    throw GateFailure("INCONCLUSIVE", std::string("nonce ledger state is malformed: ") + error.what());
  }
  const auto row = nonce_record(ledger, readset.nonce);
  if (!row.has_value()) return {candidate.manifest_sha256, readset.target_id, "AVAILABLE", readset.nonce, authz_digest, {}};
  const std::string state = json_string(*row, "state");
  if (state == "RESERVED" && allow_reserved &&
      json_string(*row, "candidate_sha256") == candidate.manifest_sha256 &&
      json_string(*row, "target_id") == readset.target_id &&
      json_string(*row, "authorization_sha256") == authz_digest &&
      json_string(*row, "protocol_version") == kAAPProtocol &&
      json_string(*row, "ledger_id") == kLedgerIdentity) {
    return {candidate.manifest_sha256, readset.target_id, "RESERVED", readset.nonce,
            authz_digest, json_string(*row, "reservation_id")};
  }
  throw GateFailure("REPLAY", "authorization nonce is already reserved or consumed");
}

#ifdef SESSION61_TESTING
void fault_point(std::string_view point) {
  const char* fault = std::getenv("M1_SESSION61_FAULT");
  if (!fault || point != fault) return;
  if (point.starts_with("crash-")) ::_exit(86);
  if (point == "eintr-write") { errno = EINTR; return; }
  if (point == "enospc-write") { errno = ENOSPC; throw GateFailure("INCONCLUSIVE", "injected ENOSPC"); }
  if (point == "eio-file-fsync" || point == "eio-dir-fsync" || point == "rename")
    throw GateFailure("INCONCLUSIVE", "injected persistence failure");
}

void write_all(int fd, std::string_view data) {
  std::size_t offset = 0;
  bool injected_eintr = false;
  while (offset < data.size()) {
    const char* fault = std::getenv("M1_SESSION61_FAULT");
    if (!injected_eintr && fault && std::string_view(fault) == "eintr-write") { injected_eintr = true; continue; }
    if (fault && std::string_view(fault) == "enospc-write") throw GateFailure("INCONCLUSIVE", "injected ENOSPC during ledger write");
    const std::size_t amount = (fault && std::string_view(fault) == "short-write") ? 1 : data.size() - offset;
    const ssize_t count = ::write(fd, data.data() + offset, amount);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) throw GateFailure("INCONCLUSIVE", "nonce ledger short write");
    offset += static_cast<std::size_t>(count);
  }
}

void sync_file(int fd, bool directory) {
  const char* fault = std::getenv("M1_SESSION61_FAULT");
  if (fault && ((!directory && std::string_view(fault) == "eio-file-fsync") ||
                (directory && std::string_view(fault) == "eio-dir-fsync")))
    throw GateFailure("INCONCLUSIVE", "injected fsync EIO");
  if (::fsync(fd) != 0) throw GateFailure("INCONCLUSIVE", "nonce ledger fsync failed");
}

void persist_test_ledger(int ledger_fd, Json::Array records) {
  Json::Object unsigned_object{{"schema_version", Json{2}}, {"protocol_version", Json{std::string(kAAPProtocol)}},
                               {"ledger_id", Json{std::string(kLedgerIdentity)}}, {"records", Json{std::move(records)}}};
  const auto sum = sha256(canonical_json(Json{unsigned_object}));
  Json::Object sealed = unsigned_object;
  sealed.emplace("sha256", Json{hex(sum.data(), sum.size())});
  const std::string output = canonical_json(Json{std::move(sealed)});
  if (output.size() > 4 * 1024 * 1024) throw GateFailure("INCONCLUSIVE", "nonce ledger capacity exceeded");
  const char* fault = std::getenv("M1_SESSION61_FAULT");
  if (fault && std::string_view(fault) == "stale-next") throw GateFailure("INCONCLUSIVE", "injected stale staging marker");
  struct stat stale{};
  if (::fstatat(ledger_fd, ".nonce-ledger.next", &stale, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT)
    throw GateFailure("INCONCLUSIVE", "nonce ledger staging state is ambiguous");
  fault_point("crash-before-stage-create");
  Fd stage(::openat(ledger_fd, ".nonce-ledger.next", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
  if (stage.get() < 0) throw GateFailure("INCONCLUSIVE", "cannot create exclusive nonce staging file");
  fault_point("crash-after-stage-create");
  write_all(stage.get(), output);
  fault_point("crash-after-stage-write");
  sync_file(stage.get(), false);
  fault_point("crash-after-file-fsync");
  stage = Fd{};
  fault_point("rename");
  if (::renameat(ledger_fd, ".nonce-ledger.next", ledger_fd, std::string(kLedgerName).c_str()) != 0)
    throw GateFailure("INCONCLUSIVE", "nonce ledger atomic rename failed");
  fault_point("crash-after-rename");
  sync_file(ledger_fd, true);
  fault_point("crash-after-dir-fsync");
}

std::string reserve_nonce_test_only(int root_fd, const GateResult& gate_result) {
  Fd gate = open_gate_directory(root_fd);
  Fd ledger_dir = open_ledger_directory(gate.get());
  Fd lock = lock_ledger(ledger_dir.get());
  Json document = read_ledger(ledger_dir.get());
  if (nonce_record(document, gate_result.nonce).has_value())
    throw GateFailure("REPLAY", "nonce is already reserved or consumed");
  Json::Array records = field(document.object(), "records").array();
  if (records.size() >= 4096)
    throw GateFailure("INCONCLUSIVE", "nonce ledger record capacity is exhausted");
  std::array<unsigned char, 16> random_id{};
  if (RAND_bytes(random_id.data(), static_cast<int>(random_id.size())) != 1)
    throw GateFailure("INCONCLUSIVE", "cannot allocate synthetic reservation ID");
  const std::string reservation_id = "res-" + hex(random_id.data(), random_id.size());
  records.push_back(Json{Json::Object{
      {"authorization_sha256", Json{gate_result.authorization_sha256}},
      {"candidate_sha256", Json{gate_result.candidate_sha256}},
      {"ledger_id", Json{std::string(kLedgerIdentity)}}, {"nonce", Json{gate_result.nonce}},
      {"protocol_version", Json{std::string(kAAPProtocol)}}, {"reservation_id", Json{reservation_id}},
      {"state", Json{"RESERVED"}}, {"target_id", Json{gate_result.target_id}}}});
  persist_test_ledger(ledger_dir.get(), std::move(records));
  return reservation_id;
}

void consume_reservation_test_only(int root_fd, const GateResult& gate_result) {
  if (gate_result.nonce_state != "RESERVED" || gate_result.reservation_id.empty())
    throw GateFailure("REPLAY", "no matching synthetic reservation is available to consume");
  Fd gate = open_gate_directory(root_fd);
  Fd ledger_dir = open_ledger_directory(gate.get());
  Fd lock = lock_ledger(ledger_dir.get());
  Json document = read_ledger(ledger_dir.get());
  const auto current = nonce_record(document, gate_result.nonce);
  if (!current.has_value() || json_string(*current, "state") != "RESERVED" ||
      json_string(*current, "reservation_id") != gate_result.reservation_id ||
      json_string(*current, "candidate_sha256") != gate_result.candidate_sha256 ||
      json_string(*current, "target_id") != gate_result.target_id ||
      json_string(*current, "authorization_sha256") != gate_result.authorization_sha256)
    throw GateFailure("REPLAY", "reservation identity no longer matches this authorization");
  Json::Array records = field(document.object(), "records").array();
  for (auto& record : records) {
    auto& row = std::get<Json::Object>(record.value);
    if (json_string(row, "nonce") == gate_result.nonce) {
      row["state"] = Json{"CONSUMED"};
      break;
    }
  }
  persist_test_ledger(ledger_dir.get(), std::move(records));
}

#ifdef SESSION62_TESTING
Json result_json(std::string status, std::string decision, std::string candidate,
                 std::string target, std::string nonce_state_value,
                 bool nonce_consumed, std::string reservation_id);
int emit_result(const Json& result, int code);

bool wait_child_bounded(pid_t child, int& status, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  do {
    const pid_t waited = ::waitpid(child, &status, WNOHANG);
    if (waited == child) return true;
    if (waited < 0 && errno != EINTR) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (std::chrono::steady_clock::now() < deadline);
  return false;
}

constexpr std::string_view kHandoffLoader = R"AAP62(import sys, os, fcntl, hashlib, json, stat, time, __future__, importlib.abc, importlib.util
from dataclasses import dataclass
from typing import Any
from enum import Enum
import re

EXPECTED = (
"qualification/alpha_m1_collector_v2/__init__.py",
"qualification/alpha_m1_collector_v2/aap_v1.py",
"qualification/alpha_m1_collector_v2/adapters.py",
"qualification/alpha_m1_collector_v2/authorization.py",
"qualification/alpha_m1_collector_v2/budget.py",
"qualification/alpha_m1_collector_v2/candidate_identity.py",
"qualification/alpha_m1_collector_v2/contracts.py",
"qualification/alpha_m1_collector_v2/nonce_ledger.py",
"qualification/alpha_m1_collector_v2/orchestrator.py",
"qualification/alpha_m1_collector_v2/results.py",
"qualification/alpha_m1_collector_v2/safe_fs.py",
"qualification/alpha_m1_collector_v2/synthetic_entry.py",
"qualification/alpha_m1_collector_v2/target_attestation.py",
"qualification/alpha_m1_collector_v2/trust_chain.py",
"qualification/alpha_m1_collector_v2/verified_launcher.py",
)
REQUIRED = fcntl.F_SEAL_SEAL | fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_GROW | fcntl.F_SEAL_WRITE
args = sys.argv[1:]
if not args or not args[0].startswith("--test-start-fd="):
    raise SystemExit("runtime start channel is missing")
start_fd = int(args.pop(0).split("=", 1)[1])
os.write(start_fd, b"S")
os.close(start_fd)
crash = bool(args and args[0] == "--test-crash-after-handoff")
timeout = bool(args and args[0] == "--test-timeout")
parent_fd_check = bool(args and args[0] == "--test-parent-fd-check")
timeout_tree = bool(args and args[0].startswith("--test-timeout-tree="))
tree_marker = args[0].split("=", 1)[1] if timeout_tree else ""
if crash or timeout or parent_fd_check:
    args = args[1:]
elif timeout_tree:
    args = args[1:]
if len(args) != len(EXPECTED):
    raise SystemExit("unexpected descriptor count")
sources = {}
digests = {}
for expected, argument in zip(EXPECTED, args, strict=True):
    parts = argument.split(":", 3)
    if len(parts) != 4:
        raise SystemExit("bad descriptor record")
    fd_text, path, size_text, digest = parts
    if path != expected or not fd_text.isdecimal() or not size_text.isdecimal() or len(digest) != 64:
        raise SystemExit("descriptor record does not match fixed allowlist")
    fd, size = int(fd_text), int(size_text)
    if fd in {int(item.split(":", 1)[0]) for item in args[:len(sources)]}:
        raise SystemExit("duplicate source descriptor")
    if size <= 0 or size > 1048576 or any(c not in "0123456789abcdef" for c in digest):
        raise SystemExit("descriptor bounds or digest encoding invalid")
    seals = fcntl.fcntl(fd, fcntl.F_GET_SEALS)
    if seals & REQUIRED != REQUIRED:
        raise SystemExit("source descriptor is not fully sealed")
    before = os.fstat(fd)
    if not stat.S_ISREG(before.st_mode) or before.st_size != size:
        raise SystemExit("source descriptor type or size invalid")
    os.lseek(fd, 0, os.SEEK_SET)
    chunks, total = [], 0
    while total < size:
        block = os.read(fd, min(65536, size - total))
        if not block:
            raise SystemExit("short source descriptor read")
        chunks.append(block)
        total += len(block)
    raw = b"".join(chunks)
    after = os.fstat(fd)
    if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns) != \
       (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns):
        raise SystemExit("source descriptor changed while being read")
    actual = hashlib.sha256(raw).hexdigest()
    if actual != digest:
        raise SystemExit("source descriptor digest mismatch")
    sources[path] = raw
    digests[path] = actual
if sum(map(len, sources.values())) > 8 * 1024 * 1024:
    raise SystemExit("handoff exceeds fixed aggregate bound")
if crash:
    os._exit(86)
if timeout:
    time.sleep(10)
if parent_fd_check:
    # Test-only observation after the native parent has completed post-fork
    # descriptor cleanup. This is not a runtime isolation claim.
    time.sleep(0.1)
    parent = os.getppid()
    if any("memfd:alpha-m1-verified-source" in os.readlink(f"/proc/{parent}/fd/{fd}")
           for fd in os.listdir(f"/proc/{parent}/fd")):
        raise SystemExit("native parent retained verified source descriptors")
if timeout_tree:
    pid = os.fork()
    if pid == 0:
        os.close(1)
        time.sleep(4)
        with open(tree_marker, "w", encoding="ascii") as marker:
            marker.write("descendant survived timeout")
        os._exit(0)
    os.close(1)
    time.sleep(10)

prefix = "qualification.alpha_m1_collector_v2"
modules_by_name = {prefix + "." + path.rsplit("/", 1)[1][:-3]: path
                   for path in EXPECTED if not path.endswith("/__init__.py")}
loaded = []
class SourceLoader(importlib.abc.Loader):
    def __init__(self, fullname, path):
        self.fullname, self.path = fullname, path
    def create_module(self, spec):
        return None
    def exec_module(self, module):
        raw = sources[self.path]
        module.__file__ = "<sealed-fd:" + self.path + ">"
        module.__cached__ = None
        module.__loader__ = self
        module.__package__ = self.fullname.rpartition(".")[0]
        exec(compile(raw, module.__file__, "exec", dont_inherit=True), module.__dict__, module.__dict__)
        loaded.append(self.path)

class FixedFinder(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname == "qualification":
            spec = importlib.util.spec_from_loader(fullname, loader=None, is_package=True)
            spec.submodule_search_locations = []
            return spec
        if fullname == prefix:
            loader = SourceLoader(fullname, EXPECTED[0])
            return importlib.util.spec_from_loader(fullname, loader, is_package=True)
        if fullname.startswith(prefix + "."):
            file = modules_by_name.get(fullname)
            if file is None:
                raise ModuleNotFoundError("application module is not in sealed allowlist: " + fullname)
            return importlib.util.spec_from_loader(fullname, SourceLoader(fullname, file))
        if "." not in fullname and fullname in {name.rsplit("/",1)[1][:-3] for name in EXPECTED}:
            raise ModuleNotFoundError("unqualified application import rejected: " + fullname)
        return None

sys.dont_write_bytecode = True
sys.path[:] = []
sys.meta_path[:] = [FixedFinder()]
for name in tuple(sys.modules):
    if name == "qualification" or name.startswith("qualification."):
        del sys.modules[name]
import qualification.alpha_m1_collector_v2
import qualification.alpha_m1_collector_v2.results
import qualification.alpha_m1_collector_v2.synthetic_entry as entry
blocked_imports = []
for attempted in (prefix + ".not_in_manifest", prefix + ".native.fake_extension", "synthetic_entry"):
    try:
        __import__(attempted)
    except ModuleNotFoundError:
        blocked_imports.append(attempted)
    else:
        raise SystemExit("unlisted app import unexpectedly succeeded")
value = entry.main()
if value != {"status": "SYNTHETIC_ONLY", "collector_started": False}:
    raise SystemExit("fixed synthetic entry returned an unexpected value")
result = {"component_sha256": digests,
          "entry_result": value,
          "entry_sha256": digests["qualification/alpha_m1_collector_v2/synthetic_entry.py"],
          "blocked_imports": blocked_imports, "loaded_modules": sorted(loaded),
          "status": "SYNTHETIC_EXECUTED"}
sys.stdout.write(json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n")
)AAP62";
constexpr std::string_view kFixedSyntheticEntrypointSha256 =
    "4fba6cdef4a0f3e338acb17560a66ac9080d374e6b3a9d30699b424027edcb20";
const std::map<std::string, std::string> kFixedSyntheticImportSources = {
    {"qualification/alpha_m1_collector_v2/__init__.py", "48b5ee16409f994ed9b14a4e0bfaa5a673cd598e402bf1c25d608b2c4f25ba1d"},
    {"qualification/alpha_m1_collector_v2/contracts.py", "99139b262a4d299e240c0c6fde66cb94acedf4b0511d12f20aff6f7ba423ecf1"},
    {"qualification/alpha_m1_collector_v2/results.py", "37e64932964375a2641ab943ac4f15d85c20181b961b167fd1dbe2d323a4a34b"},
};

void test_replace_path_after_verification(int root_fd) {
  Fd package = open_package_dir(root_fd);
  const char* original = "synthetic_entry.py";
  const char* moved = ".synthetic_entry.original";
  if (::renameat(package.get(), original, package.get(), moved) != 0)
    throw GateFailure("INCONCLUSIVE", "test path substitution rename failed");
  Fd replacement(::openat(package.get(), original,
      O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
  if (replacement.get() < 0) throw GateFailure("INCONCLUSIVE", "test replacement creation failed");
  constexpr std::string_view changed = "raise RuntimeError('pathname source must not execute')\n";
  std::size_t offset = 0;
  while (offset < changed.size()) {
    const ssize_t amount = ::write(replacement.get(), changed.data() + offset, changed.size() - offset);
    if (amount < 0 && errno == EINTR) continue;
    if (amount <= 0) throw GateFailure("INCONCLUSIVE", "test replacement write failed");
    offset += static_cast<std::size_t>(amount);
  }
  if (::fsync(replacement.get()) != 0) throw GateFailure("INCONCLUSIVE", "test replacement fsync failed");
}

void test_symlink_path_after_verification(int root_fd) {
  Fd package = open_package_dir(root_fd);
  const char* original = "synthetic_entry.py";
  const char* moved = ".synthetic_entry.original";
  if (::renameat(package.get(), original, package.get(), moved) != 0 ||
      ::symlinkat(".synthetic_entry.original", package.get(), original) != 0)
    throw GateFailure("INCONCLUSIVE", "test symlink substitution failed");
}

int handoff_synthetic_test_only(int root_fd, const Candidate& candidate, GateResult gate_result) {
  bool reservation_attempted = false;
  bool reservation_durable = false;
  bool python_started = false;
  bool consume_started = false;
  bool cleanup_incomplete = false;
  const auto entry = candidate.components.find(std::string(kEntrypointPath));
  std::string fixed_entry_digest;
  if (entry != candidate.components.end()) {
    const auto digest = sha256(entry->second);
    fixed_entry_digest = hex(digest.data(), digest.size());
  }
  if (entry == candidate.components.end() ||
      fixed_entry_digest != kFixedSyntheticEntrypointSha256)
    return emit_result(result_json("DENIED", "FIXED_SYNTHETIC_ENTRYPOINT_MISMATCH",
        gate_result.candidate_sha256, gate_result.target_id, "NOT_CHECKED", false, ""), 2);
  for (const auto& [path, expected_digest] : kFixedSyntheticImportSources) {
    const auto source = candidate.components.find(path);
    if (source == candidate.components.end())
      return emit_result(result_json("DENIED", "FIXED_IMPORT_COMPONENT_MISSING",
          gate_result.candidate_sha256, gate_result.target_id, "NOT_CHECKED", false, ""), 2);
    const auto digest = sha256(source->second);
    if (hex(digest.data(), digest.size()) != expected_digest)
      return emit_result(result_json("DENIED", "FIXED_IMPORT_COMPONENT_MISMATCH",
          gate_result.candidate_sha256, gate_result.target_id, "NOT_CHECKED", false, ""), 2);
  }
  try {
    const char* fault = std::getenv("M1_SESSION62_FAULT");
    const std::string_view fault_name = fault ? std::string_view(fault) : std::string_view{};
    if (fault_name == "crash-before-reserved") ::_exit(85);
    reservation_attempted = true;
    gate_result.reservation_id = reserve_nonce_test_only(root_fd, gate_result);
    gate_result.nonce_state = "RESERVED";
    reservation_durable = true;
    if (fault_name == "crash-after-reserved") ::_exit(86);
    if (fault_name == "replace-path-after-verification") test_replace_path_after_verification(root_fd);
    if (fault_name == "symlink-after-verification") test_symlink_path_after_verification(root_fd);

    std::vector<Fd> sources;
    std::vector<Fd> inherited;
    std::vector<std::string> handoff_args;
    sources.reserve(kFiles.size()); inherited.reserve(kFiles.size()); handoff_args.reserve(kFiles.size());
    for (std::size_t index = 0; index < kFiles.size(); ++index) {
      if (fault_name == "crash-during-handoff" && index == 1) ::_exit(87);
      const std::string& path = kFiles[index];
      const auto found = candidate.components.find(path);
      if (found == candidate.components.end()) throw GateFailure("INCONCLUSIVE", "verified source bytes missing from memory");
      Fd memfd(::memfd_create("alpha-m1-verified-source", MFD_CLOEXEC | MFD_ALLOW_SEALING));
      if (memfd.get() < 0) throw GateFailure("INCONCLUSIVE", "memfd creation failed");
      const std::string& bytes = found->second;
      std::size_t offset = 0;
      while (offset < bytes.size()) {
        if (fault_name == "short-handoff-write") {
          const ssize_t amount = ::write(memfd.get(), bytes.data() + offset, 1);
          if (amount < 0 && errno == EINTR) continue;
          if (amount <= 0) throw GateFailure("INCONCLUSIVE", "short memfd write");
          offset += static_cast<std::size_t>(amount);
        } else {
          const ssize_t amount = ::write(memfd.get(), bytes.data() + offset, bytes.size() - offset);
          if (amount < 0 && errno == EINTR) continue;
          if (amount <= 0) throw GateFailure("INCONCLUSIVE", "memfd write failed");
          offset += static_cast<std::size_t>(amount);
        }
      }
      if (::lseek(memfd.get(), 0, SEEK_SET) < 0) throw GateFailure("INCONCLUSIVE", "memfd rewind failed");
      int desired = F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE;
      if (index == 0 && fault_name == "unsealed-first") desired = 0;
      if (index == 0 && fault_name == "incomplete-seals-first") desired = F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW;
      if (desired != 0 && ::fcntl(memfd.get(), F_ADD_SEALS, desired) != 0)
        throw GateFailure("INCONCLUSIVE", "memfd sealing failed");
      if (desired != 0) {
        const int actual = ::fcntl(memfd.get(), F_GET_SEALS);
        if (actual < 0 || (actual & desired) != desired)
          throw GateFailure("INCONCLUSIVE", "memfd seal verification failed");
      }
      const int inherited_fd = ::fcntl(memfd.get(), F_DUPFD, 100);
      if (inherited_fd < 0) throw GateFailure("INCONCLUSIVE", "cannot duplicate sealed source descriptor");
      inherited.emplace_back(inherited_fd);
      const auto digest = sha256(bytes);
      const std::string digest_hex = hex(digest.data(), digest.size());
      handoff_args.push_back(std::to_string(inherited_fd) + ":" + path + ":" +
                             std::to_string(bytes.size()) + ":" + digest_hex);
      sources.push_back(std::move(memfd));
      if (fault_name == "crash-after-first-memfd" && index == 0) ::_exit(88);
    }
    if (fault_name == "closed-descriptor") inherited.front() = Fd{};
    if (fault_name == "unexpected-descriptor") handoff_args.emplace_back("999:unlisted.py:1:" + std::string(64, '0'));

    int pipe_fds[2]{};
    if (::pipe2(pipe_fds, O_CLOEXEC) != 0) throw GateFailure("INCONCLUSIVE", "handoff output pipe failed");
    Fd pipe_read(pipe_fds[0]), pipe_write(pipe_fds[1]);
    int start_fds[2]{};
    if (::pipe2(start_fds, O_CLOEXEC) != 0) throw GateFailure("INCONCLUSIVE", "runtime start channel creation failed");
    Fd start_read(start_fds[0]), start_write(start_fds[1]);
    Fd null_device(::open("/dev/null", O_RDWR | O_CLOEXEC | O_NOFOLLOW));
    if (null_device.get() < 0) throw GateFailure("INCONCLUSIVE", "cannot open fixed null device for child standard streams");
    Fd interpreter(::open(std::string(kSyntheticPython).c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    struct stat interpreter_info{};
    if (interpreter.get() < 0 || ::fstat(interpreter.get(), &interpreter_info) != 0 ||
        !S_ISREG(interpreter_info.st_mode) || (interpreter_info.st_mode & 0111) == 0)
      throw GateFailure("INCONCLUSIVE", "fixed Python interpreter descriptor is unavailable or unsafe");
    constexpr unsigned int kCloseRangeCloexec = 1U << 2;
    if (::syscall(SYS_close_range, 3U, std::numeric_limits<unsigned int>::max(), kCloseRangeCloexec) != 0)
      throw GateFailure("INCONCLUSIVE", "cannot mark unexpected descriptors close-on-exec");
    for (const auto& fd : inherited) {
      const int descriptor_flags = ::fcntl(fd.get(), F_GETFD);
      if (descriptor_flags < 0 || ::fcntl(fd.get(), F_SETFD, descriptor_flags & ~FD_CLOEXEC) != 0)
        throw GateFailure("INCONCLUSIVE", "cannot preserve an allowlisted descriptor for exec");
    }
    const int start_flags = ::fcntl(start_write.get(), F_GETFD);
    if (start_flags < 0 || ::fcntl(start_write.get(), F_SETFD, start_flags & ~FD_CLOEXEC) != 0)
      throw GateFailure("INCONCLUSIVE", "cannot preserve runtime start channel");
    const pid_t child = ::fork();
    if (child < 0) throw GateFailure("INCONCLUSIVE", "synthetic runtime fork failed");
    if (child == 0) {
      if (::setpgid(0, 0) != 0 || ::dup2(pipe_write.get(), STDOUT_FILENO) < 0 ||
          ::dup2(null_device.get(), STDIN_FILENO) < 0 || ::dup2(null_device.get(), STDERR_FILENO) < 0) ::_exit(126);
      const std::string interpreter_path(kSyntheticPython);
      std::vector<std::string> arguments = {interpreter_path, "-I", "-S", "-B", "-c", std::string(kHandoffLoader)};
      arguments.emplace_back("--test-start-fd=" + std::to_string(start_write.get()));
      if (fault_name == "crash-runtime") arguments.emplace_back("--test-crash-after-handoff");
      if (fault_name == "timeout-runtime") arguments.emplace_back("--test-timeout");
      if (fault_name == "check-parent-source-fds") arguments.emplace_back("--test-parent-fd-check");
      if (fault_name == "timeout-runtime-tree") {
        const char* marker = std::getenv("M1_SESSION62_MARKER_PATH");
        if (!marker || marker[0] != '/') ::_exit(125);
        arguments.emplace_back(std::string("--test-timeout-tree=") + marker);
      }
      arguments.insert(arguments.end(), handoff_args.begin(), handoff_args.end());
      std::vector<char*> argv;
      for (auto& argument : arguments) argv.push_back(argument.data());
      argv.push_back(nullptr);
      char env_python_no_site[] = "PYTHONNOUSERSITE=1";
      char env_no_bytecode[] = "PYTHONDONTWRITEBYTECODE=1";
      char* envp[] = {env_python_no_site, env_no_bytecode, nullptr};
      ::fexecve(interpreter.get(), argv.data(), envp);
      ::_exit(127);
    }
    // The child owns forked copies of the allowlisted descriptors. Drop every
    // source memfd in the parent immediately after fork to bound their lifetime.
    inherited.clear();
    sources.clear();
    if (::setpgid(child, child) != 0 && errno != EACCES && errno != ESRCH) {
      (void)::kill(-child, SIGKILL);
      (void)::kill(child, SIGKILL);
      int ignored = 0;
      cleanup_incomplete = !wait_child_bounded(child, ignored, std::chrono::seconds(1));
      throw GateFailure("INCONCLUSIVE", "cannot establish isolated runtime process group");
    }
    pipe_write = Fd{};
    start_write = Fd{};
    std::string child_output;
    bool output_closed = false;
    bool start_closed = false;
    bool child_reaped = false;
    bool timed_out = false;
    bool output_limited = false;
    int wait_status = 0;
    const auto kill_group = [child]() {
      if (::kill(-child, SIGKILL) != 0 && errno != ESRCH) (void)::kill(child, SIGKILL);
    };
    const auto kill_and_reap = [child, &cleanup_incomplete, &child_reaped, &wait_status]() {
      if (::kill(-child, SIGKILL) != 0 && errno != ESRCH) (void)::kill(child, SIGKILL);
      child_reaped = wait_child_bounded(child, wait_status, std::chrono::seconds(1));
      cleanup_incomplete = !child_reaped;
    };
    const auto started_at = std::chrono::steady_clock::now();
    const auto deadline = started_at + std::chrono::seconds(
        fault_name == "timeout-runtime" || fault_name == "timeout-runtime-tree" ? 2 : 10);
    std::optional<std::chrono::steady_clock::time_point> cleanup_deadline;
    bool injected_poll_error = false;
    const auto emit_runtime_failure = [&](std::string decision) {
      Json::Object result = result_json("INCONCLUSIVE", std::move(decision),
          gate_result.candidate_sha256, gate_result.target_id,
          consume_started ? "UNKNOWN_AFTER_FAILURE" : "RESERVED", false,
          gate_result.reservation_id).object();
      result["python_started"] = Json{python_started};
      result["python_start_state"] = Json{python_started ? "STARTED" :
          (child_reaped ? "NOT_STARTED" : "INCONCLUSIVE")};
      result["runtime_cleanup_incomplete"] = Json{cleanup_incomplete};
      return emit_result(Json{std::move(result)}, 2);
    };
    while (!(output_closed && start_closed && child_reaped) && !cleanup_incomplete) {
      struct pollfd descriptors[2] = {
          {output_closed ? -1 : pipe_read.get(), POLLIN | POLLHUP, 0},
          {start_closed ? -1 : start_read.get(), POLLIN | POLLHUP, 0}};
      int ready = 0;
      if (fault_name == "poll-error" && !injected_poll_error) {
        injected_poll_error = true;
        errno = EIO;
        ready = -1;
      } else {
        ready = ::poll(descriptors, 2, 100);
      }
      if (ready < 0 && errno != EINTR) {
        kill_and_reap();
        return emit_runtime_failure("HANDOFF_POLL_FAILED");
      }
      if (ready > 0 && !output_closed && (descriptors[0].revents & (POLLIN | POLLHUP)) != 0) {
        char buffer[4096];
        const ssize_t count = ::read(pipe_read.get(), buffer, sizeof(buffer));
        if (count > 0) {
          if (child_output.size() + static_cast<std::size_t>(count) > 65536) {
            output_limited = true;
            kill_group();
            cleanup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
          } else child_output.append(buffer, static_cast<std::size_t>(count));
        } else if (count == 0) output_closed = true;
        else if (errno != EINTR && errno != EAGAIN) {
          kill_and_reap();
          return emit_runtime_failure("HANDOFF_OUTPUT_READ_FAILED");
        }
      }
      if (ready > 0 && !start_closed && (descriptors[1].revents & (POLLIN | POLLHUP)) != 0) {
        char start_byte = '\0';
        const ssize_t count = ::read(start_read.get(), &start_byte, 1);
        if (count == 1 && start_byte == 'S') python_started = true;
        else if (count == 0) start_closed = true;
        else if (count < 0 && errno != EINTR && errno != EAGAIN) {
          kill_and_reap();
          return emit_runtime_failure("RUNTIME_START_CHANNEL_FAILED");
        }
      }
      if (!child_reaped) {
        const pid_t waited = ::waitpid(child, &wait_status, WNOHANG);
        if (waited == child) child_reaped = true;
        else if (waited < 0 && errno != EINTR) {
          kill_and_reap();
          return emit_runtime_failure("RUNTIME_WAIT_FAILED");
        }
      }
      const auto now = std::chrono::steady_clock::now();
      if (!timed_out && !output_limited && now >= deadline &&
          !(output_closed && start_closed && child_reaped)) {
        timed_out = true;
        kill_group();
        cleanup_deadline = now + std::chrono::seconds(1);
      }
      if (cleanup_deadline && now >= *cleanup_deadline &&
          !(output_closed && start_closed && child_reaped)) {
        cleanup_incomplete = true;
        if (!child_reaped) {
          const pid_t waited = ::waitpid(child, &wait_status, WNOHANG);
          if (waited == child) child_reaped = true;
        }
        output_closed = true;
        start_closed = true;
        pipe_read = Fd{};
        start_read = Fd{};
      }
    }
    if (timed_out || output_limited || cleanup_incomplete || !WIFEXITED(wait_status) || WEXITSTATUS(wait_status) != 0)
      return emit_runtime_failure(timed_out ? "SYNTHETIC_HANDOFF_TIMEOUT" :
          (output_limited ? "SYNTHETIC_HANDOFF_OUTPUT_LIMIT" : "SYNTHETIC_HANDOFF_FAILED"));
    if (!python_started) return emit_runtime_failure("RUNTIME_START_NOT_CONFIRMED");
    const auto emit_runtime_validation_failure = [&](std::string decision) {
      Json::Object result = result_json("INCONCLUSIVE", std::move(decision),
          gate_result.candidate_sha256, gate_result.target_id,
          "RESERVED", false, gate_result.reservation_id).object();
      result["python_started"] = Json{true};
      result["python_start_state"] = Json{"STARTED"};
      result["runtime_cleanup_incomplete"] = Json{false};
      return emit_result(Json{std::move(result)}, 2);
    };
    if (!child_output.empty() && child_output.back() == '\n') child_output.pop_back();
    if (fault_name == "alter-runtime-result") child_output = "{}";
    const Json runtime_result = JsonParser(child_output).parse();
    exact_keys(runtime_result.object(), {"blocked_imports", "component_sha256", "entry_result",
                 "entry_sha256", "loaded_modules", "status"}, "synthetic runtime result");
    Json::Object expected_digests;
    for (const auto& path : kFiles) {
      const auto digest = sha256(candidate.components.at(path));
      expected_digests.emplace(path, Json{hex(digest.data(), digest.size())});
    }
    if (canonical_json(runtime_result) != child_output ||
        json_string(runtime_result.object(), "status") != "SYNTHETIC_EXECUTED" ||
        json_string(runtime_result.object(), "entry_sha256") !=
            hex(sha256(candidate.components.at(std::string(kEntrypointPath))).data(), 32))
      return emit_runtime_validation_failure("SYNTHETIC_HANDOFF_RESULT_INVALID");
    const Json reported_digests = field(runtime_result.object(), "component_sha256");
    if (canonical_json(reported_digests) != canonical_json(Json{expected_digests}))
      return emit_runtime_validation_failure("SYNTHETIC_COMPONENT_DIGEST_MISMATCH");
    const Json::Array expected_loaded = {
        Json{"qualification/alpha_m1_collector_v2/__init__.py"},
        Json{"qualification/alpha_m1_collector_v2/contracts.py"},
        Json{"qualification/alpha_m1_collector_v2/results.py"},
        Json{"qualification/alpha_m1_collector_v2/synthetic_entry.py"}};
    const Json::Array expected_blocked = {
        Json{"qualification.alpha_m1_collector_v2.not_in_manifest"},
        Json{"qualification.alpha_m1_collector_v2.native.fake_extension"},
        Json{"synthetic_entry"}};
    if (canonical_json(field(runtime_result.object(), "loaded_modules")) != canonical_json(Json{expected_loaded}) ||
        canonical_json(field(runtime_result.object(), "blocked_imports")) != canonical_json(Json{expected_blocked}))
      return emit_runtime_validation_failure("SYNTHETIC_IMPORT_POLICY_MISMATCH");
    const Json entry_result_value = field(runtime_result.object(), "entry_result");
    const auto& entry_result = entry_result_value.object();
    exact_keys(entry_result, {"collector_started", "status"}, "fixed synthetic entry result");
    if (!std::holds_alternative<bool>(field(entry_result, "collector_started").value) ||
        std::get<bool>(field(entry_result, "collector_started").value) ||
        json_string(entry_result, "status") != "SYNTHETIC_ONLY")
      return emit_runtime_validation_failure("SYNTHETIC_ENTRY_RESULT_INVALID");
    if (fault_name == "fail-consume-dir-fsync")
      (void)::setenv("M1_SESSION61_FAULT", "eio-dir-fsync", 1);
    consume_started = true;
    consume_reservation_test_only(root_fd, gate_result);
    Json::Object result = result_json("SYNTHETIC_EXECUTED", "SYNTHETIC_TEST_ONLY_HANDOFF",
        gate_result.candidate_sha256, gate_result.target_id, "CONSUMED", true,
        gate_result.reservation_id).object();
    result["python_started"] = Json{true};
    result["runtime_result"] = runtime_result;
    return emit_result(Json{std::move(result)}, 0);
  } catch (const GateFailure& error) {
    const std::string state = error.status == "REPLAY" ? "REPLAY" :
        (consume_started || (reservation_attempted && !reservation_durable)) ? "UNKNOWN_AFTER_FAILURE" :
        reservation_durable ? "RESERVED" : "NOT_CHECKED";
    const std::string status = python_started && !consume_started ? "INCONCLUSIVE" : error.status;
    const std::string decision = python_started && !consume_started ?
        "SYNTHETIC_HANDOFF_RESULT_INVALID" : "DENIED";
    Json::Object result = result_json(status, decision, gate_result.candidate_sha256,
        gate_result.target_id, state, false, gate_result.reservation_id).object();
    result["python_started"] = Json{python_started};
    result["python_start_state"] = Json{python_started ? "STARTED" : "NOT_STARTED"};
    result["runtime_cleanup_incomplete"] = Json{cleanup_incomplete};
    return emit_result(Json{std::move(result)}, 2);
  } catch (const std::exception&) {
    const std::string state = consume_started || (reservation_attempted && !reservation_durable) ?
        "UNKNOWN_AFTER_FAILURE" : reservation_durable ? "RESERVED" : "NOT_CHECKED";
    const std::string decision = python_started && !consume_started ?
        "SYNTHETIC_HANDOFF_RESULT_INVALID" : "DENIED";
    Json::Object result = result_json("INCONCLUSIVE", decision, gate_result.candidate_sha256,
        gate_result.target_id, state, false, gate_result.reservation_id).object();
    result["python_started"] = Json{python_started};
    result["python_start_state"] = Json{python_started ? "STARTED" : "NOT_STARTED"};
    result["runtime_cleanup_incomplete"] = Json{cleanup_incomplete};
    return emit_result(Json{std::move(result)}, 2);
  }
}
#endif
#endif

Json result_json(std::string status, std::string decision, std::string candidate = {},
                 std::string target = {}, std::string nonce_state_value = "NOT_CHECKED",
                 bool nonce_consumed = false, std::string reservation_id = {}) {
  return Json{Json::Object{
      {"candidate_sha256", Json{std::move(candidate)}},
      {"decision", Json{std::move(decision)}},
      {"nonce_consumed", Json{nonce_consumed}},
      {"nonce_state", Json{std::move(nonce_state_value)}},
      {"python_started", Json{false}},
      {"reservation_id", Json{std::move(reservation_id)}},
      {"status", Json{std::move(status)}},
      {"target_id", Json{std::move(target)}},
  }};
}

int emit_result(const Json& result, int code) {
  std::cout << canonical_json(result) << '\n';
  std::cout.flush();
  if (!std::cout) return 74;
  return code;
}

int run(int argc, char** argv) {
  int test_operation = 0;
#ifdef SESSION61_TESTING
  if (argc == 2 && std::string_view(argv[1]) == "--test-canonicalize") {
    std::string input;
    char byte = '\0';
    while (std::cin.get(byte)) {
      if (input.size() >= 128 * 1024) throw GateFailure("POLICY_ERROR", "test codec input exceeds fixed size");
      input.push_back(byte);
    }
    const Json value = JsonParser(input).parse();
    std::cout << canonical_json(value) << '\n';
    std::cout.flush();
    if (!std::cout) return 74;
    return 0;
  }
#endif
  if (argc == 3 && std::string_view(argv[1]) == "--verify-only") {
    test_operation = 0;
  }
#ifdef SESSION61_TESTING
  else if (argc == 3 && std::string_view(argv[1]) == "--test-reserve-nonce") {
    test_operation = 1;
  }
  else if (argc == 3 && std::string_view(argv[1]) == "--test-consume-reservation") {
    test_operation = 2;
  }
#ifdef SESSION62_TESTING
  else if (argc == 3 && std::string_view(argv[1]) == "--test-handoff") {
    test_operation = 3;
  }
#endif
#endif
  else throw GateFailure("DENIED", "only --verify-only is supported by the candidate bootstrap");
  Fd root = open_absolute_directory(argv[2]);
  Candidate candidate;
  try { candidate = verify_manifest(root.get()); }
  catch (const GateFailure&) { throw; }
  catch (const std::exception& error) { throw GateFailure("CANDIDATE_MISMATCH", error.what()); }
  bool ledger_check_started = false;
  GateResult verified;
  try {
    verified = verify_native_gate(root.get(), candidate, test_operation == 2, &ledger_check_started);
  } catch (const GateFailure& error) {
    const std::string nonce_status = error.status == "REPLAY" ? "REPLAY" :
        (ledger_check_started && error.status == "INCONCLUSIVE" ? "INCONCLUSIVE" : "NOT_CHECKED");
    return emit_result(result_json(error.status, "DENIED", candidate.manifest_sha256,
                                   {}, nonce_status, false), 2);
  } catch (const std::exception&) {
    const std::string nonce_status = ledger_check_started ? "INCONCLUSIVE" : "NOT_CHECKED";
    const std::string status = ledger_check_started ? "INCONCLUSIVE" : "POLICY_ERROR";
    return emit_result(result_json(status, "DENIED", candidate.manifest_sha256,
                                   {}, nonce_status, false), 2);
  }
#ifdef SESSION62_TESTING
  if (test_operation == 3) return handoff_synthetic_test_only(root.get(), candidate, std::move(verified));
#endif
#ifdef SESSION61_TESTING
  if (test_operation == 1) {
    try {
      verified.reservation_id = reserve_nonce_test_only(root.get(), verified);
    } catch (const GateFailure& error) {
      return emit_result(result_json(error.status, "DENIED", verified.candidate_sha256,
                                     verified.target_id,
                                     error.status == "REPLAY" ? "REPLAY" : "UNKNOWN_AFTER_FAILURE", false), 2);
    } catch (const std::exception&) {
      return emit_result(result_json("INCONCLUSIVE", "DENIED", verified.candidate_sha256,
                                     verified.target_id, "UNKNOWN_AFTER_FAILURE", false), 2);
    }
    return emit_result(result_json("RESERVED", "SYNTHETIC_TEST_ONLY", verified.candidate_sha256,
                                   verified.target_id, "RESERVED", false, verified.reservation_id), 0);
  }
  if (test_operation == 2) {
    try {
      consume_reservation_test_only(root.get(), verified);
    } catch (const GateFailure& error) {
      return emit_result(result_json(error.status, "DENIED", verified.candidate_sha256,
                                     verified.target_id,
                                     error.status == "REPLAY" ? "REPLAY" : "UNKNOWN_AFTER_FAILURE", false,
                                     verified.reservation_id), 2);
    } catch (const std::exception&) {
      return emit_result(result_json("INCONCLUSIVE", "DENIED", verified.candidate_sha256,
                                     verified.target_id, "UNKNOWN_AFTER_FAILURE", false,
                                     verified.reservation_id), 2);
    }
    return emit_result(result_json("CONSUMED", "SYNTHETIC_TEST_ONLY", verified.candidate_sha256,
                                   verified.target_id, "CONSUMED", true, verified.reservation_id), 0);
  }
#else
  (void)test_operation;
#endif
  return emit_result(result_json("VERIFIED", "VERIFY_ONLY_NO_EXECUTION", verified.candidate_sha256,
                                 verified.target_id,
                                 verified.nonce_state == "AVAILABLE" ? "AVAILABLE" : verified.nonce_state, false,
                                 verified.reservation_id), 0);
}

}  // namespace

int main(int argc, char** argv) {
  try { return run(argc, argv); }
  catch (const GateFailure& error) {
    const std::string nonce_status = error.status == "REPLAY" ? "REPLAY" : "NOT_CHECKED";
    return emit_result(result_json(error.status, "DENIED", {}, {}, nonce_status, false), 2);
  } catch (const std::exception&) {
    return emit_result(result_json("POLICY_ERROR", "DENIED", {}, {}, "NOT_CHECKED", false), 2);
  }
}
