#pragma once

#include "types.hpp"
#include "repository.hpp"
#include "install_plan.hpp" // core::plan_install(InstallRequest) returns the immutable public plan.
#include "resolver.hpp"
#include <optional>
#include <string>
#include <vector>

namespace athena::core::api {

inline constexpr unsigned source_api_major = 1;

enum class ErrorCode {
    InvalidRequest, Configuration, Repository, Resolution, Integrity,
    Build, Store, Conflict, Activation, Recovery, Permission, Concurrency,
    Unsupported
};

struct Error {
    ErrorCode code = ErrorCode::Unsupported;
    std::string message;
    std::string context;
};

template<class T>
struct Response {
    std::optional<T> value;
    std::optional<Error> error;
    explicit operator bool() const noexcept { return value.has_value() && !error; }
};

struct PackageRequest {
    std::vector<PackageId> packages;
    std::string profile;
    TargetEnvironment target;
};

struct PlanResponse {
    std::string plan_id;
    std::vector<PackageId> selected;
    std::string base_generation;
};

struct TransactionResponse {
    std::string transaction_id;
    std::string generation_id;
    bool committed = false;
    bool recovery_required = false;
};

// Source-level API contract. Implementations are supplied by the application layer.
class PackageManager {
public:
    virtual ~PackageManager() = default;
    virtual Response<PlanResponse> plan_install(const PackageRequest&) = 0;
    virtual Response<TransactionResponse> install(const PackageRequest&) = 0;
    virtual Response<TransactionResponse> remove(const PackageRequest&) = 0;
    virtual Response<TransactionResponse> recover(const std::string& target_id) = 0;
};

} // namespace athena::core::api
