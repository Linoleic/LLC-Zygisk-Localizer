#include "../tools/update_policy.hpp"

#include <cassert>
#include <cstdio>
#include <limits>
#include <string>

using resource_update::Json;

template <typename Function>
static bool throws(Function function) {
    try {
        function();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

static Json remote(const Json& value) {
    return resource_update::metadata(std::string("{\"version\":") + value.dump() + "}");
}

static std::string state(const Json& report) {
    return report["state"].get<std::string>();
}

int main() {
    using namespace resource_update;

    // Integer and string versions are normalized to exact decimal strings.
    assert(version(Json(2026101602)) == "2026101602");
    assert(version(Json("2026101602")) == "2026101602");
    assert(metadata(R"({"version":2026101602})")["version"] == "2026101602");
    assert(metadata(R"({"version":"2026101602","notice":"更新"})")["notice"] == "更新");

    // Reject malformed, partial, NUL-containing, and wrongly typed metadata.
    assert(throws([] { metadata(""); }));
    assert(throws([] { metadata("not-json"); }));
    assert(throws([] { metadata(R"({"version":2026)"); }));
    std::string nul_metadata = R"({"version":2026})";
    nul_metadata.push_back('\0');
    nul_metadata += "{}";
    assert(throws([&] { metadata(nul_metadata); }));
    assert(throws([] { metadata(R"({"version":null})"); }));
    assert(throws([] { metadata(R"({"version":2026,"notice":7})"); }));
    assert(throws([] { metadata("[]"); }));
    assert(throws([] { metadata(R"("metadata")"); }));
    assert(throws([] { metadata(R"({"version":9,"notice":"a\u0000b"})"); }));
    assert(throws([] { version(Json(0)); }));
    assert(throws([] { version(Json(-9)); }));
    assert(throws([] { version(Json(9.5)); }));
    assert(throws([] { version(Json(true)); }));
    assert(throws([] { version(Json("")); }));
    assert(throws([] { version(Json(" 9")); }));
    assert(throws([] { version(Json("9 ")); }));
    assert(throws([] { version(Json("9e1")); }));
    assert(throws([] { version(Json("01")); }));
    assert(throws([] { version(Json("1234567890123456789")); }));

    const int64_t now = 1'800'000'000;
    Json local = {{"state", "read"}, {"version", "9"}};
    Json initial_cache = initial();
    assert(state(summary(initial_cache, local, now)) == "not_checked");
    assert(state(summary(initial_cache, local, now, true)) == "manager_busy");

    // Compare canonical decimal versions by value, never lexicographically.
    auto checking9 = begin(initial_cache, now);
    auto success9 = succeeded(checking9, remote(Json(9)), local, now);
    assert(state(summary(success9, local, now)) == "no_update");
    auto success10 = succeeded(begin(initial_cache, now), remote(Json("10")), local, now);
    assert(state(summary(success10, local, now)) == "update_available");
    auto success8 = succeeded(begin(initial_cache, now), remote(Json(8)), local, now);
    assert(state(summary(success8, local, now)) == "local_ahead");
    auto local_unavailable = local;
    local_unavailable.erase("version");
    assert(state(summary(success10, local_unavailable, now)) == "comparison_unavailable");
    local_unavailable["version"] = nullptr;
    assert(state(summary(success10, local_unavailable, now)) == "comparison_unavailable");

    // Failure changes attempt status but preserves the entire last success.
    Json failed_cache = failed(begin(success10, now + 10), "network_error", now + 11);
    assert(state(summary(failed_cache, local, now + 11)) == "check_failed");
    assert(failed_cache["last_success"] == success10["last_success"]);
    assert(failed_cache["last_success"]["checked_at_unix"] == now);
    assert(failed_cache["last_attempt"]["error"] == "network_error");

    // Busy output is checking; a persisted checking attempt is interrupted when idle.
    Json checking_cache = begin(success10, now + 20);
    assert(state(summary(checking_cache, local, now + 21, true)) == "checking");
    assert(state(summary(checking_cache, local, now + 21, false)) == "check_interrupted");
    assert(checking_cache["last_success"] == success10["last_success"]);

    // A result becomes stale strictly after 24 hours; clock rollback is stale too.
    assert(state(summary(success10, local, now + fresh_seconds)) == "update_available");
    assert(state(summary(success10, local, now + fresh_seconds + 1)) == "stale_result");
    Json rolled_back = summary(success10, local, now - 1);
    assert(state(rolled_back) == "stale_result");
    assert(rolled_back["comparison"]["age_seconds"].is_null());

    // Invalid cache structures and unsupported status/source are rejected.
    assert(throws([] { validate_cache(Json::object()); }));
    auto bad_schema = initial();
    bad_schema["schema"] = 2;
    assert(throws([&] { validate_cache(bad_schema); }));
    auto bad_attempt = initial();
    bad_attempt["last_attempt"] = {{"status", "unknown"}, {"started_at_unix", 1}};
    assert(throws([&] { validate_cache(bad_attempt); }));
    auto bad_success = success10;
    bad_success["last_success"]["remote"]["endpoint"] = "https://example.invalid/";
    assert(throws([&] { validate_cache(bad_success); }));
    bad_success = success10;
    bad_success["last_success"]["checked_at_unix"] = -1;
    assert(throws([&] { validate_cache(bad_success); }));
    bad_success = success10;
    bad_success["last_success"]["checked_at_unix"] = std::numeric_limits<uint64_t>::max();
    assert(throws([&] { validate_cache(bad_success); }));
    bad_success = success10;
    bad_success["last_success"]["remote"].erase("metadata_sha256");
    assert(throws([&] { validate_cache(bad_success); }));
    bad_success = success10;
    bad_success["last_success"]["remote"]["metadata_sha256"] = std::string(64, 'G');
    assert(throws([&] { validate_cache(bad_success); }));

    std::puts("update_policy: metadata validation, version comparison, and cache lifecycle passed");
    return 0;
}
