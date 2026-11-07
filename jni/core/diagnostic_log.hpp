#pragma once
#include "warning_summary.hpp"
#include "il2cpp_api.hpp"
#include "../../third_party/nlohmann/json.hpp"
#include <chrono>
#include <mutex>

namespace diagnostic_log {
inline warning_summary::State state;
inline std::mutex mutex;
inline uint64_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void emit(const std::vector<warning_summary::Line>& lines) {
    for (const auto& line : lines) {
        auto examples = nlohmann::json(line.group.examples).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (line.recovered) LOGI("[LLC-DIAG] table=%s reason=%s resolved suppressed=%llu", line.table.c_str(), line.reason.c_str(), (unsigned long long)line.suppressed);
        else LOGW("[LLC-DIAG] table=%s reason=%s count=%llu samples=%s suppressed=%llu", line.table.c_str(), line.reason.c_str(),
            (unsigned long long)line.group.count, examples.c_str(), (unsigned long long)line.suppressed);
    }
}
inline void submit(const std::string& table, const warning_summary::Batch& batch, bool complete = true) {
    std::lock_guard<std::mutex> lock(mutex); emit(state.submit(table, batch, now(), complete));
}
inline void flush() { std::lock_guard<std::mutex> lock(mutex); emit(state.flush(now())); }
inline void reset() { std::lock_guard<std::mutex> lock(mutex); state.reset(); }
struct Scope {
    std::string table;
    warning_summary::Batch batch;
    explicit Scope(const char* name) : table(name) {}
    ~Scope() { submit(table, batch); }
    void add(const char* reason, const std::string& id) { batch[reason].add(id); }
};
}
