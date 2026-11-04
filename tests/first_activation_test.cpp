#define LLC_MANAGER_TEST
#include "../tools/resource_manager.cpp"
#include <cassert>
#include <fstream>

namespace fs = std::filesystem;
static void put(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << bytes;
}
int main() {
    auto root = fs::temp_directory_path() / ("llc-first-activation-" + manager::token());
    manager::DownloadContext ctx;
    ctx.private_root = root / "private";
    ctx.app_files = root / "app/files";
    ctx.packs = ctx.app_files / "Lang/.llc-packs";
    ctx.legacy = ctx.app_files / "Lang/LLC_zh-CN";
    fs::create_directories(ctx.private_root);
    fs::create_directories(ctx.app_files);
    manager::Store store(ctx.private_root);
    auto dir = ctx.private_root / "candidates/candidate-1-2";
    auto source = dir / "pack";
    put(source / "Info/version.json", R"({"version":"123"})");
    put(source / "Info/LICENSE", "test fixture license");
    put(source / "UI.json", R"({"dataList":[{"id":"key","content":"translated"}]})");
    put(source / "Skills.json", R"({"dataList":[{"id":301,"levelList":[{"level":1,"name":"skill","desc":"description"}]}]})");
    put(source / "Passive.json", R"({"dataList":[{"id":401,"name":"passive","desc":"description"}]})");
    put(source / "SkillTag.json", R"({"dataList":[{"id":"tag","name":"tag"}]})");
    put(source / "BattleHint.json", R"({"dataList":[{"id":"1","content":"hint"}]})");
    manager::Json plan = {{"version","123"},{"url","https://github.com/LocalizeLimbusCompany/LocalizeLimbusCompany/releases/download/123/LimbusLocalize_123.zip"},
        {"metadata_endpoint",release_plan::api("123")},{"source","official_github_release_asset"},{"name","LimbusLocalize_123.zip"},
        {"sha256",std::string(64,'b')},{"size",1},{"asset_id",13},{"release_id",1},{"updated_at","2026-10-29T00:00:00Z"}};
    auto doc = resource_pack::document(resource_pack::make_manifest(source,"123").bytes);
    doc["upstream"] = plan;
    auto manifest = resource_pack::parse_manifest(doc.dump());
    put(dir / "manifest.json", manifest.bytes);
    auto state = manager::transfer_initial();
    state["prepared"] = {{"id","candidate-1-2"},{"key",manifest.key},{"plan",plan}};
    manager::save_transfer(store,state);
    assert(manager::managed_summary(ctx.packs,ctx.legacy)["can_initialize"] == true);
    // An existing legacy pack must still be imported, including malformed data.
    put(ctx.legacy / "Info/version.json", "damaged");
    assert(manager::managed_summary(ctx.packs,ctx.legacy)["can_initialize"] == false);
    auto blocked = manager::operate(store,"activate",ctx);
    assert(blocked["last_attempt"]["error"] == "import_current_required_for_rollback_baseline");
    fs::remove(ctx.legacy / "Info/version.json");
    // A failed first transaction leaves no active state; retry can reuse publication.
    bool reached_commit = false;
    try {
        manager::activate(store,state,ctx,[&](const char* point) {
            if (std::string(point) == "before_state") {
                reached_commit = true;
                throw std::runtime_error("interrupted");
            }
        });
        assert(false);
    } catch (const std::exception&) {}
    assert(reached_commit);
    assert(!fs::exists(ctx.packs / "state.json"));
    auto result = manager::operate(store,"activate",ctx);
    if (result["last_attempt"]["phase"] != "succeeded") std::cerr << result.dump(2) << '\n';
    assert(result["last_attempt"]["phase"] == "succeeded");
    assert(resource_pack::read_state(ctx.packs).active == manifest.key);
    assert(resource_pack::read_state(ctx.packs).previous.empty());
    assert(resource_pack::select(ctx.packs,ctx.legacy).key == manifest.key);
    assert(manager::operate(store,"activate",ctx)["last_attempt"]["phase"] == "succeeded");
    put(ctx.packs / "state.json", "damaged");
    assert(manager::operate(store,"activate",ctx)["last_attempt"]["phase"] == "failed");
    fs::remove_all(root);
}
