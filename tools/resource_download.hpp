#pragma once
#include "release_plan.hpp"
#include "zip_pack.hpp"
#include "../jni/core/json_loader.hpp"
#include "../jni/core/storage.hpp"
#include "../jni/core/translation_evidence.hpp"
#include <sys/statvfs.h>
#ifndef LLC_PACK_NO_MAIN
#define LLC_PACK_NO_MAIN
#define LLC_DOWNLOAD_UNDEF_PACK_MAIN
#endif
#include "resource_pack.cpp"
#ifdef LLC_DOWNLOAD_UNDEF_PACK_MAIN
#undef LLC_PACK_NO_MAIN
#undef LLC_DOWNLOAD_UNDEF_PACK_MAIN
#endif

// Included after resource_manager's private store/transport primitives. Neither
// this pipeline nor the local publisher is linked into the injected core.
namespace manager {
struct DownloadContext {
    fs::path private_root = state_root;
    fs::path packs = resource_paths::store_root;
    fs::path legacy = resource_paths::legacy_root;
    fs::path app_files = "/data/user/0/com.ProjectMoon.LimbusCompany/files";
};
inline Json transfer_initial() { return {{"schema", 1}, {"kind", "resource_download_state"}, {"last_attempt", nullptr}, {"prepared", nullptr}}; }
inline void validate_transfer(const Json& state) {
    require(state.is_object() && state.value("schema", Json()) == 1 && state.value("kind", Json()) == "resource_download_state" &&
        state.contains("last_attempt") && state.contains("prepared"), "invalid_download_state");
    if (!state["prepared"].is_null()) {
        const auto& candidate = state["prepared"];
        require(candidate.is_object() && candidate.contains("plan"), "invalid_prepared_candidate"); release_plan::validate(candidate["plan"]);
        auto id = candidate.value("id", std::string{});
        require(resource_pack::identifier(id) && id.rfind("candidate-", 0) == 0 && resource_pack::identifier(candidate.value("key", std::string{})), "invalid_candidate_identity");
    }
    if (!state["last_attempt"].is_null()) {
        auto& attempt = state["last_attempt"];
        require(attempt.is_object() && attempt.contains("operation") && attempt["operation"].is_string() && attempt.contains("phase") &&
            attempt["phase"].is_string() && attempt.contains("started_at_unix") && timestamp(attempt["started_at_unix"]), "invalid_transfer_attempt");
        auto operation = attempt["operation"].get<std::string>(), phase = attempt["phase"].get<std::string>();
        require(operation == "download" || operation == "activate" || operation == "import-current" || operation == "rollback", "invalid_transfer_operation");
        require(phase == "working" || phase == "downloading" || phase == "validating" || phase == "succeeded" || phase == "failed", "invalid_transfer_phase");
    }
}
inline Json read_transfer(const Store& store) {
    auto result = store.read_document("download-state.json", transfer_initial()); validate_transfer(result); return result;
}
inline void save_transfer(Store& store, const Json& state) { validate_transfer(state); store.write_document("download-state.json", state); }
inline void phase(Store& store, Json& state, const char* value) { state["last_attempt"]["phase"] = value; save_transfer(store, state); }
inline void free_space(const fs::path& path, uint64_t bytes) {
    struct statvfs space{}; require(!statvfs(path.c_str(), &space), "space_query_failed");
    require(space.f_frsize && space.f_bavail >= (bytes + 32 * 1024 * 1024) / space.f_frsize + 1, "insufficient_storage_space");
}
inline void private_folder(const fs::path& path) { Fd fd(Store::open_directory(path)); }
inline Json loader_check(const fs::path& root) {
    g_ui_map.clear(); g_skill_tags.clear(); g_battle_hints.clear(); g_personalities.clear(); g_passives.clear();
    g_bufs.clear(); g_battle_keywords.clear(); g_egos.clear(); g_characters.clear(); g_skills.clear(); g_voice_dlgs.clear();
    g_theater_map.clear(); g_banner_map.clear(); g_stage_chapter_map.clear(); g_stage_node_map.clear(); g_bgm_lyrics.clear(); g_dungeon_nodes.clear();
    auto& registry = translation_evidence::registry; registry = translation_evidence::Registry{};
    load_all_ui_jsons(root.c_str()); load_all_content_jsons(root.c_str()); registry.rebuild();
    auto files = registry.file_report();
    require(files["rejected_files"] == 0 && files["index_model_issue_files"] == 0, "candidate_loader_file_or_model_error");
    require(!g_ui_map.empty() && !g_skills.empty() && !g_passives.empty() && !g_skill_tags.empty() && !g_battle_hints.empty(), "candidate_missing_core_dictionaries");
    size_t mismatches = 0, rows = 0;
    for (const auto& table : registry.sources) for (const auto& row : table.second.rows) {
        ++rows;
        mismatches += registry.classify(table.first, row.first) == translation_evidence::RowState::MappingGap;
    }
    require(!mismatches, "candidate_source_loader_mismatch");
    return {{"file_checks", files}, {"indexed_source_rows", rows}, {"source_map_mismatches", mismatches},
        {"scope", "file_integrity_and_four_known_models_not_whole_game_coverage"}};
}
inline void require_version(const fs::path& root, const std::string& expected) {
    auto info = document(resource_pack::read_bytes(root / "Info/version.json", 16384));
    require(info.contains("version") && version(info["version"]) == expected, "archive_resource_version_mismatch");
    require(fs::is_regular_file(fs::symlink_status(root / "Info/LICENSE")), "archive_license_missing");
}
inline Json obtain_plan(const Fetch& get_version, const std::function<std::string(const std::string&)>& get_release) {
    auto remote = metadata(get_version()); auto v = remote["version"].get<std::string>();
    return release_plan::parse(get_release(release_plan::api(v)), v);
}
inline Json prepare_verified(Store& store, Json& state, const DownloadContext& context, const Json& plan, const fs::path& archive) {
    require(store.locked && store.root == context.private_root, "candidate_store_mismatch_or_busy");
    release_plan::validate(plan);
    auto bytes = read_path(archive, zip_pack::archive_limit);
    require(bytes.size() == plan["size"].get<uint64_t>() && resource_pack::digest(bytes) == plan["sha256"], "archive_size_or_sha256_mismatch");
    auto entries = zip_pack::catalog(bytes); uint64_t output_size = 0;
    for (const auto& entry : entries) if (!entry.relative.empty()) output_size += entry.size;
    free_space(context.private_root, output_size * 2);
    private_folder(context.private_root / "candidates");
    auto id = "candidate-" + token(); auto directory = context.private_root / "candidates" / id, source = directory / "pack";
    require(fs::create_directory(directory), "candidate_directory_exists"); private_folder(directory); private_folder(source);
    phase(store, state, "validating");
    for (const auto& entry : entries) {
        if (entry.relative.empty()) continue;
        auto content = zip_pack::inflate(bytes, entry);
        auto target = source / entry.relative;
        pack_tool::directory(target.parent_path()); pack_tool::write_new(target, content);
    }
    auto v = plan["version"].get<std::string>(); require_version(source, v);
    auto loader = loader_check(source);
    auto raw_manifest = resource_pack::make_manifest(source, v);
    auto manifest_doc = resource_pack::document(raw_manifest.bytes);
    manifest_doc["origin"] = "verified-github-release-asset"; manifest_doc["upstream"] = plan;
    auto manifest = resource_pack::parse_manifest(manifest_doc.dump(2) + "\n");
    pack_tool::write_new(directory / "manifest.json", manifest.bytes);
    resource_pack::verify(source, manifest, false);
    for (const auto& member : fs::recursive_directory_iterator(directory)) if (member.is_directory()) pack_tool::sync_directory(member.path());
    pack_tool::sync_directory(directory); pack_tool::sync_directory(context.private_root / "candidates");
    return {{"id", id}, {"key", manifest.key}, {"plan", plan}, {"files", manifest.files.size()}, {"uncompressed_bytes", output_size},
        {"verified_at_unix", std::time(nullptr)}, {"loader_check", loader}, {"activation_rechecks_pack", true}};
}
inline Json download(Store& store, Json& state, const DownloadContext& context) {
    auto get_release = [](const std::string& url) { return https_client::text(url, 1024 * 1024); };
    auto observed_plan = [&] {
        auto observed = check(store, local(context.packs, context.legacy), fetch_metadata, [] { return static_cast<int64_t>(std::time(nullptr)); });
        require(observed["last_attempt"]["status"] == "succeeded", "upstream_version_check_failed");
        auto v = observed["last_success"]["remote"]["version"].get<std::string>();
        return release_plan::parse(get_release(release_plan::api(v)), v);
    };
    auto plan = observed_plan();
    state["last_attempt"]["plan"] = plan; phase(store, state, "downloading");
    private_folder(context.private_root / "downloads"); free_space(context.private_root, plan["size"].get<uint64_t>());
    auto name = "asset-" + std::to_string(plan["asset_id"].get<uint64_t>()) + "-" + plan["sha256"].get<std::string>() + ".zip";
    auto archive = context.private_root / "downloads" / name;
    bool cached = false;
    if (fs::exists(fs::symlink_status(archive))) {
        auto old = read_path(archive, zip_pack::archive_limit);
        cached = old.size() == plan["size"].get<uint64_t>() && resource_pack::digest(old) == plan["sha256"];
        require(cached, "cached_archive_integrity_failed");
    }
    if (!cached) {
        auto partial = fs::path(archive.string() + ".part-" + token());
        Fd file(open(partial.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
        uint64_t received = 0;
        auto progress = std::chrono::steady_clock::now();
        try {
            auto count = https_client::stream(plan["url"], plan["size"].get<uint64_t>(), 120, true,
                [&](const char* data, size_t amount) {
                    write_fd(file.value, std::string(data, amount)); received += amount;
                    if (std::chrono::steady_clock::now() - progress >= std::chrono::seconds(1)) {
                        state["last_attempt"]["bytes_received"] = received; save_transfer(store, state);
                        progress = std::chrono::steady_clock::now();
                    }
                });
            state["last_attempt"]["bytes_received"] = received;
            require(count == plan["size"].get<uint64_t>() && !fsync(file.value), "archive_download_incomplete");
            require(resource_pack::digest(read_path(partial, zip_pack::archive_limit)) == plan["sha256"], "archive_sha256_mismatch");
            require(!rename(partial.c_str(), archive.c_str()), "archive_publish_failed"); pack_tool::sync_directory(archive.parent_path());
        } catch (...) { state["last_attempt"]["bytes_received"] = received; unlink(partial.c_str()); throw; }
    }
    // Pin the tag/asset ID/size/digest before and after fetching the bytes. A
    // mutable release is acceptable only for this unchanged observed identity.
    auto after = observed_plan();
    require(release_plan::same(plan, after), "release_changed_during_download");
    auto candidate = prepare_verified(store, state, context, plan, archive);
    state["prepared"] = candidate;
    return {{"candidate_key", candidate["key"]}, {"files", candidate["files"]}, {"archive_reused", cached}, {"active_resources_changed", false}};
}
inline Json import_current(const DownloadContext& context) {
    if (fs::exists(fs::symlink_status(context.packs / "state.json"))) {
        auto selected = resource_pack::select(context.packs, context.legacy);
        return {{"key", selected.key}, {"already_initialized", true}, {"requires_cold_start", true}};
    }
    auto info = document(resource_pack::read_bytes(context.legacy / "Info/version.json", 16384));
    auto v = version(info.at("version")); require_version(context.legacy, v); auto loader = loader_check(context.legacy);
    auto manifest = resource_pack::make_manifest(context.legacy, "import-" + v);
    uint64_t total = 0; for (const auto& item : manifest.files) total += item.size;
    free_space(context.private_root, total);
    auto snapshot = context.private_root / ("import-manifest-" + token() + ".json"); pack_tool::write_new(snapshot, manifest.bytes);
    pack_tool::Access access(context.app_files);
    resource_pack::State empty;
    auto key = pack_tool::install(context.legacy, snapshot, context.packs, {}, &access, &empty);
    return {{"key", key}, {"files", manifest.files.size()}, {"loader_check", loader}, {"requires_cold_start", true}, {"legacy_directory_retained", true}};
}
inline Json activate(Store& store, const Json& state, const DownloadContext& context, const pack_tool::Checkpoint& hook = {}) {
    require(store.locked && store.root == context.private_root, "activation_store_mismatch_or_busy"); validate_transfer(state);
    require(!state["prepared"].is_null(), "no_prepared_candidate");
    require(fs::exists(fs::symlink_status(context.packs / "state.json")), "import_current_required_for_rollback_baseline");
    auto before = resource_pack::read_state(context.packs);
    auto current = resource_pack::select(context.packs, context.legacy);
    require(current.key == (current.mode == "active" ? before.active : before.previous), "activation_selection_changed");
    auto candidate = state["prepared"]; release_plan::validate(candidate["plan"]);
    auto directory = context.private_root / "candidates" / candidate["id"].get<std::string>(), source = directory / "pack";
    require(fs::is_directory(fs::symlink_status(context.private_root / "candidates")) && fs::is_directory(fs::symlink_status(directory)), "candidate_directory_unavailable");
    auto manifest = resource_pack::parse_manifest(read_path(directory / "manifest.json", resource_pack::manifest_limit));
    auto origin = resource_pack::document(manifest.bytes);
    require(manifest.key == candidate["key"] && origin.value("upstream", Json()) == candidate["plan"], "candidate_provenance_mismatch");
    auto v = candidate["plan"]["version"].get<std::string>(); require_version(source, v);
    auto active_info = document(resource_pack::read_bytes(fs::path(current.root) / "Info/version.json", 16384));
    auto active_version = version(active_info.at("version"));
    require(v.size() > active_version.size() || (v.size() == active_version.size() && v >= active_version), "candidate_downgrade_refused");
    resource_pack::verify(source, manifest, false); auto loader = loader_check(source);
    uint64_t total = 0; for (const auto& item : manifest.files) total += item.size; free_space(context.packs, total);
    pack_tool::Access access(context.app_files);
    auto key = pack_tool::install(source, directory / "manifest.json", context.packs, hook, &access, &before);
    auto after = resource_pack::read_state(context.packs);
    return {{"key", key}, {"previous_key", after.previous}, {"requires_cold_start", true}, {"loader_check", loader}};
}
inline Json operate(Store& store, const std::string& operation, const DownloadContext& context = {}) {
    require(store.locked && store.root == context.private_root, "resource_operation_busy_or_context_mismatch");
    auto state = read_transfer(store);
    state["last_attempt"] = {{"operation", operation}, {"phase", "working"}, {"started_at_unix", std::time(nullptr)}, {"finished_at_unix", nullptr}, {"error", nullptr}};
    save_transfer(store, state);
    try {
        Json result;
        if (operation == "download") result = download(store, state, context);
        else if (operation == "import-current") result = import_current(context);
        else if (operation == "activate") result = activate(store, state, context);
        else if (operation == "rollback") result = {{"key", pack_tool::rollback(context.packs)}, {"requires_cold_start", true}};
        else throw std::runtime_error("unknown_resource_operation");
        state["last_attempt"]["result"] = std::move(result); state["last_attempt"]["phase"] = "succeeded";
    } catch (const std::exception& error) { state["last_attempt"]["phase"] = "failed"; state["last_attempt"]["error"] = error.what(); }
    state["last_attempt"]["finished_at_unix"] = std::time(nullptr); save_transfer(store, state); return state;
}
inline Json transfer_summary(const Json& state, const Json& local_version, bool busy) {
    validate_transfer(state); Json result = state;
    std::string status = "not_started";
    if (!state["last_attempt"].is_null()) {
        auto phase = state["last_attempt"]["phase"].get<std::string>();
        status = phase == "failed" || phase == "succeeded" ? phase : busy ? phase : "operation_interrupted";
        if (!busy && phase != "succeeded" && phase != "failed" && state["last_attempt"]["operation"] == "activate" &&
            !state["prepared"].is_null() && local_version.value("pack_key", Json()) == state["prepared"]["key"]) status = "activation_committed";
    }
    result["state"] = status; result["auto_activation"] = false;
    return result;
}
} // namespace manager
