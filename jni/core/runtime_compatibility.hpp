#pragma once
#include "il2cpp_api.hpp"
#include "sha256.hpp"
#include "compatibility_policy.hpp"
#include "../third_party/nlohmann/json.hpp"
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <sstream>
#include <fstream>

inline nlohmann::json g_runtime_compatibility_report = nlohmann::json::object();

inline std::string process_start_ticks() {
    std::ifstream file("/proc/self/stat");
    std::string line;
    std::getline(file, line);
    auto closing = line.rfind(") ");
    if (closing == std::string::npos) return {};
    std::istringstream fields(line.substr(closing+2));
    std::string value;
    for (int i=0;i<=19;++i) if (!(fields >> value)) return {};
    return value;
}

inline nlohmann::json load_compatibility_config() {
    const char* candidate_paths[] = {
        "/data/adb/modules/llc_zygisk/config/compatibility.json",
        "/data/local/tmp/llc/compatibility.json",
        "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_compatibility_config.json"
    };
    for (const char* path : candidate_paths) {
        FILE* fp = fopen(path, "rb");
        if (!fp) continue;
        fseek(fp, 0, SEEK_END);
        long len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (len <= 0 || len > 65536) { fclose(fp); continue; }
        std::string buffer(len, '\0');
        size_t read_bytes = fread(&buffer[0], 1, len, fp);
        fclose(fp);
        if (read_bytes == static_cast<size_t>(len)) {
            auto j = nlohmann::json::parse(buffer, nullptr, false);
            if (j.is_object()) return j;
        }
    }
    return nlohmann::json{{"allow_adaptive_compatibility", true}};
}

// Called before resolving/invoking managed APIs, creating the engine thread,
// reading object layouts, loading a font bundle, or replacing managed text.
inline bool verify_runtime_compatibility() {
    auto started = std::chrono::steady_clock::now();
    std::string digest, path, error;
    uint64_t bytes = 0;
    void* library = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_NOW);
    Dl_info info{};
    void* symbol = library ? dlsym(library, "il2cpp_init") : nullptr;
    if (!symbol || !dladdr(symbol, &info) || !info.dli_fname) error = "loaded_il2cpp_path_unavailable";
    else {
        path = info.dli_fname;
        FILE* file = fopen(path.c_str(), "rb");
        struct stat before{}, after{};
        if (!file) error = "loaded_il2cpp_file_unreadable";
        else {
            if (fstat(fileno(file), &before) || !S_ISREG(before.st_mode) || before.st_size <= 0) error = "loaded_il2cpp_not_regular_file";
            else {
                BuildSha256 hash;
                uint8_t buffer[65536];
                size_t n;
                while ((n = fread(buffer, 1, sizeof(buffer), file)) != 0) { hash.update(buffer,n); bytes += n; }
                if (ferror(file) || fstat(fileno(file), &after) || before.st_size != after.st_size ||
                    before.st_mtime != after.st_mtime || bytes != static_cast<uint64_t>(before.st_size)) error = "loaded_il2cpp_read_incomplete_or_changed";
                else digest = hash.hex_digest();
            }
            fclose(file);
        }
    }
    if (library) dlclose(library);

    nlohmann::json custom_cfg = load_compatibility_config();
    CompatibilityVerdict verdict = error.empty() ?
        evaluate_compatibility(digest, custom_cfg) :
        CompatibilityVerdict{false, "fingerprint_unavailable", "", "", ""};
    bool approved = verdict.approved;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
    nlohmann::json report = {{"schema",1},{"kind","compatibility"},{"pid",getpid()},
        {"process_start_ticks",process_start_ticks()}, {"status", verdict.status},
        {"runtime_modifications_allowed", approved}, {"loaded_library", path}, {"il2cpp_sha256", digest},
        {"verified_il2cpp_sha256", approved ? nlohmann::json(verdict.matched_sha256) : nlohmann::json(nullptr)},
        {"verified_game_label", approved ? nlohmann::json(verdict.label) : nlohmann::json(nullptr)},
        {"validation_scope", approved ? nlohmann::json(verdict.scope) : nlohmann::json(nullptr)},
        {"bytes_hashed", bytes}, {"elapsed_ms",elapsed}, {"error", error.empty() ? nlohmann::json(nullptr) : nlohmann::json(error)}};
    g_runtime_compatibility_report = report;
    const char* output = "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_compatibility.json";
    std::string temporary = std::string(output) + ".next";
    if (FILE* file = fopen(temporary.c_str(), "wb")) {
        std::string text = report.dump(2);
        bool ok = fwrite(text.data(),1,text.size(),file) == text.size();
        if (fclose(file)) ok = false;
        if (!ok || rename(temporary.c_str(),output)) unlink(temporary.c_str());
    }
    LOGI("[LLC-COMPAT] status=%s sha256=%s bytes=%llu elapsed_ms=%lld modifications_allowed=%d error=%s",
         report["status"].get<std::string>().c_str(), digest.c_str(), (unsigned long long)bytes,
         (long long)elapsed, approved, error.c_str());
    if (!approved) LOGW("[LLC-COMPAT] Core modification paths disabled; revalidation is required for this native build.");
    return approved;
}
