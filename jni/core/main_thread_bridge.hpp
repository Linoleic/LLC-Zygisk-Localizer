#pragma once
#include "managed_fields.hpp"
#include "../../third_party/nlohmann/json.hpp"
#include <mutex>
#include <unistd.h>
#include <cstdio>

// A version-checked IL2CPP delegate bridge. No method bodies are modified.
// The native ABI must pass a local Invoke test before anything is posted.
namespace main_thread_bridge {
using Handle = uintptr_t;
using NewHandle = Handle (*)(Il2CppObject*, bool);
using GetTarget = Il2CppObject* (*)(Handle);
using FreeHandle = void (*)(Handle);
using MethodObject = Il2CppObject* (*)(const MethodInfo*, Il2CppClass*);
inline NewHandle new_handle = nullptr;
inline GetTarget get_target = nullptr;
inline FreeHandle free_handle = nullptr;
inline MethodObject method_object = nullptr;
inline Handle delegate_handle = 0, context_handle = 0;
inline const MethodInfo* post_method = nullptr;
inline const MethodInfo* current_context = nullptr;
inline const MethodInfo* current_thread = nullptr;
inline const MethodInfo* managed_id = nullptr;
inline void* context_main_id = nullptr;
inline std::mutex state_mutex;
using MainAction = const char* (*)();
struct State {
    std::string request, output;
    std::string kind;
    nlohmann::json action_context = nlohmann::json::object();
    bool pending = false, done = false, published = false, self_test = false;
    unsigned publication_attempts = 0;
    bool publication_abandoned = false;
    int sender = 0, callback = 0, managed = 0, main = 0, self_calls = 0;
    bool context_equal = false;
    Il2CppObject* expected_self_state = nullptr;
    const char* error = nullptr;
    MainAction action = nullptr;
    bool action_attempted = false;
};
inline State state;

inline const char* type_name(const Il2CppType* type) {
    auto* klass = type ? il2cpp_class_from_type_fn(type) : nullptr;
    return klass ? il2cpp_class_get_name_fn(klass) : "";
}
inline void* field_in_hierarchy(Il2CppClass* klass, const char* name) {
    for (auto* k = klass; k; k = il2cpp_class_get_parent_fn(k)) {
        auto* field = il2cpp_class_get_field_from_name_fn(k, name);
        if (field) return field;
    }
    return nullptr;
}

inline void native_callback(void*, Il2CppObject* argument, const MethodInfo*) {
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (state.self_test) {
            ++state.self_calls;
            state.callback = gettid();
            if (argument != state.expected_self_state) state.error = "delegate_state_abi_mismatch";
            return;
        }
        if (!state.pending) return;
    }
    int actual_id = 0, main_id = 0;
    MainAction action = nullptr;
    Il2CppException* exception = nullptr;
    auto* context = il2cpp_runtime_invoke_fn(current_context, nullptr, nullptr, &exception);
    bool equal = !exception && context && context == get_target(context_handle);
    auto* expected = get_target(context_handle);
    if (expected) il2cpp_field_get_value_fn(expected, context_main_id, &main_id);
    auto* thread = il2cpp_runtime_invoke_fn(current_thread, nullptr, nullptr, &exception);
    if (!exception && thread) {
        auto* boxed = il2cpp_runtime_invoke_fn(managed_id, thread, nullptr, &exception);
        if (!exception && boxed && strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(boxed)), "Int32") == 0)
            actual_id = *static_cast<int*>(il2cpp_object_unbox_fn(boxed));
    }
    const char* action_error = nullptr;
    bool valid = !exception && equal && main_id > 0 && actual_id == main_id;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        valid = valid && gettid() != state.sender;
        if (valid) { action = state.action; state.action_attempted = action != nullptr; }
    }
    if (action) action_error = action();
    std::lock_guard<std::mutex> lock(state_mutex);
    state.callback = gettid();
    state.managed = actual_id;
    state.main = main_id;
    state.context_equal = equal;
    if (!valid)
        state.error = "unity_main_thread_identity_not_verified";
    else if (action_error) state.error = action_error;
    state.pending = false;
    state.done = true;
}

inline const char* initialize() {
    if (delegate_handle && context_handle) return nullptr;
    static_assert(sizeof(uintptr_t) == 8, "bridge is arm64 only");
    void* library = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_NOW);
    if (!library) return "il2cpp_not_loaded";
    new_handle = reinterpret_cast<NewHandle>(dlsym(library, "il2cpp_gchandle_new"));
    get_target = reinterpret_cast<GetTarget>(dlsym(library, "il2cpp_gchandle_get_target"));
    free_handle = reinterpret_cast<FreeHandle>(dlsym(library, "il2cpp_gchandle_free"));
    method_object = reinterpret_cast<MethodObject>(dlsym(library, "il2cpp_method_get_object"));
    dlclose(library);
    if (!new_handle || !get_target || !free_handle || !method_object) return "optional_bridge_exports_missing";
    auto* awaitable = find_class_any("UnityEngine", "Awaitable");
    auto* callback_class = find_class_any("System.Threading", "SendOrPostCallback");
    auto* delegate_class = find_class_any("System", "Delegate");
    auto* awaiter = find_class_any("UnityEngine", "MainThreadAwaitable");
    auto* sync = find_class_any("System.Threading", "SynchronizationContext");
    auto* thread_class = find_class_any("System.Threading", "Thread");
    if (!awaitable || !callback_class || !delegate_class || !awaiter || !sync || !thread_class) return "bridge_types_missing";
    auto* context_field = field_in_hierarchy(awaitable, "_synchronizationContext");
    if (!context_field || !(il2cpp_field_get_flags_fn(context_field) & 0x10) ||
        strcmp(type_name(il2cpp_field_get_type_fn(context_field)), "SynchronizationContext")) return "awaitable_context_schema_mismatch";
    Il2CppObject* context = nullptr;
    il2cpp_field_static_get_value_fn(context_field, &context);
    if (!context || strcmp(il2cpp_class_get_name_fn(il2cpp_object_get_class_fn(context)), "UnitySynchronizationContext")) return "unity_context_unavailable";
    auto* context_class = il2cpp_object_get_class_fn(context);
    context_main_id = field_in_hierarchy(context_class, "m_MainThreadID");
    if (!context_main_id || strcmp(type_name(il2cpp_field_get_type_fn(context_main_id)), "Int32")) return "main_thread_id_schema_mismatch";
    post_method = il2cpp_class_get_method_from_name_fn(context_class, "Post", 2);
    current_context = il2cpp_class_get_method_from_name_fn(sync, "get_Current", 0);
    current_thread = il2cpp_class_get_method_from_name_fn(thread_class, "get_CurrentThread", 0);
    managed_id = il2cpp_class_get_method_from_name_fn(thread_class, "get_ManagedThreadId", 0);
    auto* method = il2cpp_class_get_method_from_name_fn(awaiter, "DoOnCompleted", 1);
    auto* invoke = il2cpp_class_get_method_from_name_fn(callback_class, "Invoke", 1);
    if (!post_method || !current_context || !current_thread || !managed_id || !method || !invoke ||
        il2cpp_method_is_instance_fn(method) || strcmp(type_name(il2cpp_method_get_return_type_fn(method)), "Void") ||
        strcmp(type_name(il2cpp_method_get_param_fn(method, 0)), "Object") ||
        strcmp(type_name(il2cpp_method_get_param_fn(post_method, 0)), "SendOrPostCallback") ||
        strcmp(type_name(il2cpp_method_get_param_fn(post_method, 1)), "Object")) return "bridge_method_schema_mismatch";
    const MethodInfo* create = nullptr;
    void* iter = nullptr;
    while (auto* candidate = il2cpp_class_get_methods_fn(delegate_class, &iter)) {
        if (strcmp(il2cpp_method_get_name_fn(candidate), "CreateDelegate") || il2cpp_method_is_instance_fn(candidate) ||
            il2cpp_method_get_param_count_fn(candidate) != 2) continue;
        if (!strcmp(type_name(il2cpp_method_get_param_fn(candidate, 0)), "Type") &&
            !strcmp(type_name(il2cpp_method_get_param_fn(candidate, 1)), "MethodInfo")) { create = candidate; break; }
    }
    if (!create) return "create_delegate_overload_missing";
    Handle context_root = new_handle(context, false);
    if (!context_root) return "context_root_failed";
    auto* reflected = method_object(method, awaiter);
    if (!reflected) { free_handle(context_root); return "reflection_method_unavailable"; }
    Handle method_root = new_handle(reflected, false);
    if (!method_root) { free_handle(context_root); return "method_root_failed"; }
    auto* type = il2cpp_type_get_object_fn(il2cpp_class_get_type_fn(callback_class));
    Il2CppException* exception = nullptr;
    void* args[] = {type, get_target(method_root)};
    auto* callback = il2cpp_runtime_invoke_fn(create, nullptr, args, &exception);
    free_handle(method_root);
    if (exception || !callback || il2cpp_object_get_class_fn(callback) != callback_class) {
        free_handle(context_root); return "create_delegate_failed";
    }
    Handle callback_root = new_handle(callback, false);
    if (!callback_root) { free_handle(context_root); return "delegate_root_failed"; }
    auto* ptr_field = field_in_hierarchy(callback_class, "method_ptr");
    auto* invoke_field = field_in_hierarchy(callback_class, "invoke_impl");
    if (!ptr_field || !invoke_field || strcmp(type_name(il2cpp_field_get_type_fn(ptr_field)), "IntPtr") ||
        strcmp(type_name(il2cpp_field_get_type_fn(invoke_field)), "IntPtr") ||
        (il2cpp_field_get_flags_fn(ptr_field) & 0x10) || (il2cpp_field_get_flags_fn(invoke_field) & 0x10)) {
        free_handle(callback_root); free_handle(context_root); return "delegate_pointer_schema_mismatch";
    }
    void* native = reinterpret_cast<void*>(native_callback);
    callback = get_target(callback_root);
    il2cpp_field_set_value_fn(callback, ptr_field, &native);
    il2cpp_field_set_value_fn(callback, invoke_field, &native);
    auto* token = il2cpp_string_new_fn("llc_delegate_abi_probe");
    Handle token_root = new_handle(reinterpret_cast<Il2CppObject*>(token), false);
    if (!token_root) { free_handle(callback_root); free_handle(context_root); return "selftest_root_failed"; }
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        state.self_test = true;
        state.self_calls = 0;
        state.expected_self_state = get_target(token_root);
    }
    void* invoke_args[] = {get_target(token_root)};
    il2cpp_runtime_invoke_fn(invoke, get_target(callback_root), invoke_args, &exception);
    bool passed;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        state.self_test = false;
        passed = !exception && state.self_calls == 1 && !state.error && state.callback == gettid();
        state.expected_self_state = nullptr;
    }
    free_handle(token_root);
    if (!passed) { free_handle(callback_root); free_handle(context_root); return "delegate_invoke_selftest_failed"; }
    // Root these two objects for the lifetime of this bridge/process, including queued callbacks.
    delegate_handle = callback_root;
    context_handle = context_root;
    return nullptr;
}

inline bool busy() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return state.pending || (state.done && !state.published && !state.publication_abandoned);
}

inline void request_probe(const char* output, const char* request, MainAction action = nullptr, const char* kind = nullptr,
                          const nlohmann::json& context = nlohmann::json::object()) {
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (state.pending || (state.done && !state.published && !state.publication_abandoned)) return;
        state = State{};
        state.request = request ? request : "";
        state.output = output;
        state.sender = gettid();
        state.action = action;
        state.kind = kind ? kind : action ? "story_refresh" : "main_thread_probe";
        state.action_context = context;
    }
    const char* error = initialize();
    if (error) {
        std::lock_guard<std::mutex> lock(state_mutex);
        state.error = error; state.done = true; return;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        state.pending = true;
    }
    Il2CppException* exception = nullptr;
    void* args[] = {get_target(delegate_handle), nullptr};
    il2cpp_runtime_invoke_fn(post_method, get_target(context_handle), args, &exception);
    if (exception) {
        std::lock_guard<std::mutex> lock(state_mutex);
        state.error = "post_threw"; state.pending = false; state.done = true;
    }
}

inline void publish_result() {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (!state.done || state.published || state.publication_abandoned) return;
    ++state.publication_attempts;
    nlohmann::json result = {{"schema", 1}, {"kind", state.kind}, {"request_id", state.request},
        {"pid", getpid()}, {"status", state.error ? "failed" : "passed"},
        {"error", state.error ? nlohmann::json(state.error) : nlohmann::json(nullptr)},
        {"native_sender_tid", state.sender}, {"native_callback_tid", state.callback},
        {"managed_callback_id", state.managed}, {"unity_main_id", state.main},
        {"context_matches", state.context_equal}, {"delegate_abi_selftest", delegate_handle != 0},
        {"action_attempted", state.action_attempted}, {"action_applied", state.action && state.action_attempted && !state.error}};
    if (!state.action_context.empty()) result["action_context"] = state.action_context;
    auto temporary = state.output + ".next";
    auto text = result.dump(2);
    if (FILE* file = fopen(temporary.c_str(), "wb")) {
        bool ok = fwrite(text.data(), 1, text.size(), file) == text.size();
        if (fclose(file)) ok = false;
        if (ok && !rename(temporary.c_str(), state.output.c_str())) state.published = true;
        else unlink(temporary.c_str());
    }
    if (!state.published && state.publication_attempts >= 3) {
        state.publication_abandoned = true;
        LOGE("[LLC-MAIN] Result publication failed after 3 attempts; releasing bridge request=%s", state.request.c_str());
    }
}
} // namespace main_thread_bridge
