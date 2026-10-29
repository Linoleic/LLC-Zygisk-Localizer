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
    bool approved = error.empty() && approved_native_build(digest);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
    nlohmann::json report = {{"schema",1},{"kind","compatibility"},{"pid",getpid()},
        {"process_start_ticks",process_start_ticks()}, {"status", approved ? "approved" : (error.empty() ? "unknown_native_build" : "fingerprint_unavailable")},
        {"runtime_modifications_allowed", approved}, {"loaded_library", path}, {"il2cpp_sha256", digest},
        {"verified_il2cpp_sha256", approved ? nlohmann::json(digest) : nlohmann::json(nullptr)},
        {"verified_game_label", approved ? nlohmann::json(approved_native_label(digest)) : nlohmann::json(nullptr)},
        {"validation_scope", approved ? nlohmann::json(native_validation_scope(digest)) : nlohmann::json(nullptr)},
        {"bytes_hashed", bytes}, {"elapsed_ms",elapsed}, {"error", error.empty() ? nlohmann::json(nullptr) : nlohmann::json(error)}};
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
