#include "../tools/https_client.hpp"
#include "../tools/update_policy.hpp"
#include "../tools/release_plan.hpp"

#include <cassert>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>

using Json = nlohmann::json;

template <typename Function>
static bool throws(Function function) {
    try {
        function();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

// === Helpers from update_policy_test.cpp ===
static Json remote(const Json& value) {
    return resource_update::metadata(std::string("{\"version\":") + value.dump() + "}");
}

static std::string state(const Json& report) {
    return report["state"].get<std::string>();
}

// === Helpers from release_plan_test.cpp ===
static Json release_asset() {
    return Json{{"id", 646068962}, {"name", "LimbusLocalize_2026101602.zip"},
        {"state", "uploaded"}, {"size", 5978331},
        {"digest", "sha256:130080d86b7dbd12df7f9fd9bc15310ad6dab15b6ab182ef9bfcf604f0462f2c"},
        {"updated_at", "2026-10-16T03:58:55Z"},
        {"browser_download_url", "https://github.com/LocalizeLimbusCompany/LocalizeLimbusCompany/releases/download/2026101602/LimbusLocalize_2026101602.zip"}};
}

static Json release_document() {
    return Json{{"id", 414522502}, {"tag_name", "2026101602"}, {"draft", false},
        {"prerelease", false}, {"assets", Json::array({release_asset()})}};
}

static Json plan() { return release_plan::parse(release_document().dump(), "2026101602"); }

static void test_https_status() {
using https_client::http_reason;
    const std::string api = "https://api.github.com/repos/a/b/releases/tags/1";
    assert(std::string(http_reason("\nLLC_HTTP_STATUS:403", api)) == "github_api_access_denied");
    assert(std::string(http_reason("\nLLC_HTTP_STATUS:403", "https://github.com/a")) == "https_access_denied");
    assert(std::string(http_reason("\nLLC_HTTP_STATUS:429", api)) == "https_rate_limited");
    assert(std::string(http_reason("\nLLC_HTTP_STATUS:404", api)) == "https_not_found");
    assert(std::string(http_reason("\nLLC_HTTP_STATUS:503", api)) == "https_server_error");
    assert(std::string(http_reason("broken", api)) == "https_http_error");
    assert(std::string(http_reason("\nLLC_HTTP_STATUS:401", api)) == "https_http_error");
}

static void test_update_policy() {
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
    return;
}

static void test_release_plan() {
using namespace release_plan;

    const auto raw = release_document().dump();
    Json good = parse(raw, "2026101602");
    assert(good["version"] == "2026101602");
    assert(good["release_id"] == 414522502);
    assert(good["asset_id"] == 646068962);
    assert(good["size"] == 5978331);
    assert(good["sha256"] == "130080d86b7dbd12df7f9fd9bc15310ad6dab15b6ab182ef9bfcf604f0462f2c");
    assert(good["url"] == "https://github.com/LocalizeLimbusCompany/LocalizeLimbusCompany/releases/download/2026101602/LimbusLocalize_2026101602.zip");
    assert(good["metadata_endpoint"] == "https://api.github.com/repos/LocalizeLimbusCompany/LocalizeLimbusCompany/releases/tags/2026101602");
    assert(api("2026101602") == good["metadata_endpoint"]);
    assert(same(good, good));

    assert(throws([] { api("../2026101602"); }));
    assert(throws([] { api("01"); }));
    assert(throws([] { parse("not json", "2026101602"); }));
    assert(throws([&] { parse(raw, "2026101603"); }));
    std::string nul = raw; nul.push_back('\0');
    assert(throws([&] { parse(nul, "2026101602"); }));
    assert(throws([&] { parse(std::string(1024 * 1024 + 1, ' '), "2026101602"); }));

    auto changed = release_document(); changed["tag_name"] = "2026101603";
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["draft"] = true;
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["prerelease"] = true;
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["assets"] = "not an array";
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["assets"] = Json::array({release_asset(), release_asset()});
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["assets"][0]["name"] = "LimbusLocalize_2026101603.zip";
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["assets"][0]["state"] = "new";
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["assets"][0]["browser_download_url"] = "https://evil.invalid/a.zip";
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["id"] = 0;
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["assets"][0]["id"] = -1;
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["assets"][0]["size"] = 0;
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["assets"][0]["size"] = archive_limit + 1;
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["assets"][0]["size"] = 1.5;
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["assets"][0].erase("digest");
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["assets"][0]["digest"] = "sha1:130080d86b7dbd12df7f9fd9bc15310ad6dab15b6ab182ef9bfcf604f0462f2c";
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["assets"][0]["digest"] = "sha256:ABC";
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));
    changed = release_document(); changed["assets"][0].erase("updated_at");
    assert(throws([&] { parse(changed.dump(), "2026101602"); }));

    Json altered = plan(); altered["url"] = "https://attacker.invalid/LimbusLocalize_2026101602.zip";
    assert(throws([&] { validate(altered); }));
    altered = plan(); altered["metadata_endpoint"] = "https://attacker.invalid/release";
    assert(throws([&] { validate(altered); }));
    altered = plan(); altered["source"] = "untrusted";
    assert(throws([&] { validate(altered); }));
    altered = plan(); altered["name"] = "payload.zip";
    assert(throws([&] { validate(altered); }));
    altered = plan(); altered["sha256"] = "bad";
    assert(throws([&] { validate(altered); }));
    altered = plan(); altered["size"] = archive_limit + 1;
    assert(throws([&] { validate(altered); }));
    altered = plan(); altered["asset_id"] = 0;
    assert(throws([&] { validate(altered); }));
    altered = plan(); altered["release_id"] = -1;
    assert(throws([&] { validate(altered); }));
    altered = plan(); altered["updated_at"] = std::string(65, 'x');
    assert(throws([&] { validate(altered); }));
    assert(throws([&] { same(plan(), altered); }));
    altered = plan(); altered["asset_id"] = 646068963;
    assert(!same(plan(), altered));

    Json direct_plan = direct("2026101602");
    assert(direct_plan["version"] == "2026101602");
    assert(direct_plan["name"] == "LimbusLocalize_2026101602.zip");
    assert(direct_plan["url"] == good["url"]);
    assert(direct_plan["metadata_endpoint"] == good["metadata_endpoint"]);
    assert(direct_plan["source"] == "official_github_release_asset");
    assert(direct_plan.value("direct_release", false) == true);
    direct_plan["size"] = 5978331;
    direct_plan["sha256"] = "130080d86b7dbd12df7f9fd9bc15310ad6dab15b6ab182ef9bfcf604f0462f2c";
    direct_plan["updated_at"] = "direct_release";
    validate(direct_plan);
    assert(same(direct_plan, direct_plan));
}

int main() {
    test_https_status();
    test_update_policy();
    test_release_plan();
    std::puts("All update plan tests passed successfully.");
    return 0;
}
