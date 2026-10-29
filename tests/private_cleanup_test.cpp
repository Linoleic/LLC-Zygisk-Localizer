#define LLC_MANAGER_TEST
#include "../tools/resource_manager.cpp"
#include "../tools/private_cleanup.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
static void put(const fs::path& p, const std::string& value="x") { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << value; }
int main() {
    auto base=fs::temp_directory_path()/ ("llc-private-cleanup-"+manager::token());
    auto root=base/"store", game=base/"game"; fs::create_directories(root); put(game/"releases/sentinel","keep");
    {
        manager::Store store(root);
        auto transfer=manager::transfer_initial();
        transfer["prepared"]={{"id","candidate-9-9"},{"key","pack-key"},{"plan",{{"version","123"},{"url","https://github.com/LocalizeLimbusCompany/LocalizeLimbusCompany/releases/download/123/LimbusLocalize_123.zip"},{"metadata_endpoint","https://api.github.com/repos/LocalizeLimbusCompany/LocalizeLimbusCompany/releases/tags/123"},{"source","official_github_release_asset"},{"name","LimbusLocalize_123.zip"},{"sha256",std::string(64,'b')},{"size",1},{"asset_id",13},{"release_id",1},{"updated_at","2026-10-29T00:00:00Z"}}}};
        manager::save_transfer(store,transfer);
        put(root/"candidates/candidate-1-2/pack/a");
        put(root/"candidates/candidate-9-9/keep");
        put(root/"downloads/asset-12-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.zip");
        put(root/"downloads/asset-13-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.zip");
        put(root/"downloads/asset-13-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.zip.part-3-4");
        put(root/"import-manifest-5-6.json"); put(root/"unknown.tmp");
        fs::create_directories(root/"candidates/candidate-7-8"); fs::create_symlink(game,root/"candidates/candidate-7-8/link");
        fs::create_directory(root/"releases"); put(root/"releases/untouched");
        auto p=manager::cleanup_plan(store); assert(p["kind"]=="module_cleanup_plan" && p["token"].get<std::string>().size()==64);
        { manager::Store another(root); assert(!another.locked); }
        assert(manager::cleanup_plan(store)["token"] == p["token"]); // Opening another reader must not invalidate a preview.
        bool has_candidate=false, has_archive=false, has_prepared_archive=false, has_part=false, has_import=false;
        for(const auto& x:p["items"]) { auto id=x["id"].get<std::string>(); has_candidate|=id=="candidates/candidate-1-2"; has_archive|=id=="downloads/asset-12-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.zip"; has_prepared_archive|=id=="downloads/asset-13-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.zip"; has_part|=id=="downloads/asset-13-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.zip.part-3-4"; has_import|=id=="import-manifest-5-6.json"; }
        assert(has_candidate&&has_archive&&!has_prepared_archive&&has_part&&has_import);
        auto stale=p["token"].get<std::string>(); put(root/"unknown.tmp","changed");
        bool expired=false; try { manager::cleanup_apply(store,stale); } catch(const std::exception& e) { expired=std::string(e.what())=="cleanup_preview_changed"; }
        assert(expired && fs::exists(root/"candidates/candidate-1-2/pack/a"));
        auto refreshed=manager::cleanup_plan(store);
        auto result=manager::cleanup_apply(store,refreshed["token"]);
        assert(result["phase"]=="succeeded");
        assert(!fs::exists(root/"candidates/candidate-1-2"));
        assert(fs::exists(root/"candidates/candidate-9-9/keep"));
        assert(fs::exists(root/"downloads/asset-13-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.zip"));
        assert(fs::exists(root/"candidates/candidate-7-8/link"));
        assert(fs::exists(root/"unknown.tmp")&&fs::exists(root/"releases/untouched"));
        assert(fs::exists(game/"releases/sentinel"));
    }
    fs::remove_all(base);
}
