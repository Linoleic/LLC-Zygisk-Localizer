#pragma once
#include "il2cpp_api.hpp"
#include "resource_pack.hpp"
#include "text_schema_policy.hpp"
#include <chrono>
#include <algorithm>

// Metadata only, on the already-attached worker. Never enumerate live objects,
// get unknown field values, invoke game methods, or add translation targets.
namespace text_schema {
using Json = nlohmann::json;
inline std::set<std::string> registered;
inline std::string catalog_signature;
inline void configure(std::set<std::string> names) {
    registered = std::move(names);
    catalog_signature = resource_pack::digest(Json({{"registered", registered}, {"aliases", "List_Text_suffix_v1"}}).dump());
}
inline Json collect(Il2CppClass* dataset, const std::string& native_hash) {
    Json report = {{"schema", 1}, {"kind", "text_schema_snapshot"}, {"status", "unavailable"}, {"read_only", true},
        {"scope", "TextDataSet_fields_and_TextData_classes_in_its_assembly_not_all_game_text"},
        {"catalog_signature", catalog_signature}, {"native_sha256", native_hash},
        {"containers", Json::array()}, {"classes", Json::array()}, {"missing_registered", Json::array()}};
    void* library = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    if (!library || !dataset) { if (library) dlclose(library); return report; }
    using TypeName = char* (*)(const Il2CppType*);
    using Free = void (*)(void*);
    using Image = const Il2CppImage* (*)(Il2CppClass*);
    auto type_name = reinterpret_cast<TypeName>(dlsym(library, "il2cpp_type_get_name"));
    auto release = reinterpret_cast<Free>(dlsym(library, "il2cpp_free"));
    auto image_of = reinterpret_cast<Image>(dlsym(library, "il2cpp_class_get_image"));
    if (!type_name || !release || !image_of || !il2cpp_image_get_class_count_fn || !il2cpp_image_get_class_fn) {
        dlclose(library); report["error"] = "metadata_exports_unavailable"; return report;
    }
    const auto started = std::chrono::steady_clock::now();
    bool complete = true;
    auto budget = [&] { return std::chrono::steady_clock::now() - started < std::chrono::milliseconds(1500); };
    auto type = [&](void* field) {
        char* raw = type_name(il2cpp_field_get_type_fn(field));
        std::string value;
        if (raw) { auto length = strnlen(raw, 2049); if (length > 2048) complete = false; value.assign(raw, std::min<size_t>(length, 2048)); release(raw); }
        else complete = false;
        return value;
    };
    auto fields = [&](Il2CppClass* klass, size_t limit) {
        Json rows = Json::array(); std::set<std::string> seen;
        size_t depth = 0;
        for (; klass && depth < 8 && complete; klass = il2cpp_class_get_parent_fn(klass), ++depth) {
            void* iterator = nullptr;
            while (void* field = il2cpp_class_get_fields_fn(klass, &iterator)) {
                if (!budget() || rows.size() >= limit) { complete = false; break; }
                if (il2cpp_field_get_flags_fn(field) & 0x10) continue;
                const char* name = il2cpp_field_get_name_fn(field);
                if (!name || strnlen(name, 241) > 240) { complete = false; break; }
                if (!seen.insert(name).second) continue;
                rows.push_back({{"name", name}, {"type", type(field)}, {"offset", il2cpp_field_get_offset_fn(field)}});
            }
        }
        if (klass && depth == 8) complete = false;
        std::sort(rows.begin(), rows.end(), [](const Json& a, const Json& b) { return a.at("name") < b.at("name"); });
        return rows;
    };
    auto all_fields = fields(dataset, 512);
    if (all_fields.empty()) complete = false;
    std::set<std::string> present;
    for (auto row : all_fields) {
        auto name = row["name"].get<std::string>(), typename_ = row["type"].get<std::string>();
        present.insert(name);
        if (text_schema_policy::candidate_field(registered, name, typename_)) {
            row["registered"] = text_schema_policy::registered_field(registered, name); report["containers"].push_back(row);
        }
    }
    for (const auto& name : registered) {
        bool found = false;
        for (const auto& field : present) if (text_schema_policy::registered_field({name}, field)) { found = true; break; }
        if (!found) report["missing_registered"].push_back(name);
    }
    auto* image = image_of(dataset);
    if (!image) complete = false;
    const auto count = image ? il2cpp_image_get_class_count_fn(image) : 0;
    if (!count || count > 100000) complete = false;
    const char* image_name = image ? il2cpp_image_get_name_fn(image) : nullptr;
    report["assembly"] = image_name ? Json(image_name) : Json(nullptr);
    size_t scanned = 0;
    for (; complete && scanned < count; ++scanned) {
        if (!budget()) { complete = false; break; }
        auto* klass = il2cpp_image_get_class_fn(image, scanned); if (!klass) continue;
        const char* name = il2cpp_class_get_name_fn(klass); if (!name || strncmp(name, "TextData_", 9)) continue;
        if (report["classes"].size() >= 512) { complete = false; break; }
        auto members = fields(klass, 128); bool strings = false;
        for (const auto& member : members) if (member["type"] == "System.String") strings = true;
        const char* ns = il2cpp_class_get_namespace_fn(klass);
        if (strnlen(name, 241) > 240 || (ns && strnlen(ns, 241) > 240)) { complete = false; break; }
        if (strings) report["classes"].push_back({{"name", (ns && *ns ? std::string(ns) + "." : std::string{}) + name}, {"fields", members}});
    }
    std::sort(report["classes"].begin(), report["classes"].end(), [](const Json& a, const Json& b) { return a.at("name") < b.at("name"); });
    report["scanned_classes"] = scanned;
    report["elapsed_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    report["status"] = complete ? "complete" : "partial";
    dlclose(library);
    return report;
}
}
