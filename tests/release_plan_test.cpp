#include "../tools/release_plan.hpp"

#include <cassert>
#include <exception>
#include <string>

using release_plan::Json;

template <typename Function>
static bool throws(Function function) {
    try { function(); } catch (const std::exception&) { return true; }
    return false;
}

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

int main() {
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
}
