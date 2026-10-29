// Separate diagnostic core: never link with the localization engine.
// Enumerates exported IL2CPP metadata only; no object dereference, game method
// invocation, field-value access, managed text allocation, or patching APIs.
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>
#include "../jni/third_party/nlohmann/json.hpp"

using json = nlohmann::json;
static constexpr const char* output = "/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/llc_metadata_probe.json";
static void publish(json& report) {
    std::string next = std::string(output) + ".next";
    if (FILE* f = fopen(next.c_str(), "wb")) {
        auto text = report.dump(2);
        bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
        if (fclose(f)) ok = false;
        if (!ok || rename(next.c_str(), output)) unlink(next.c_str());
    }
    __android_log_print(ANDROID_LOG_INFO, "LLC_META", "metadata probe status=%s pid=%d", report["status"].get<std::string>().c_str(), getpid());
}
static void* collect(void*) {
    __android_log_print(ANDROID_LOG_INFO, "LLC_META", "metadata probe waiting for startup pid=%d", getpid());
    // Seeing libil2cpp mapped does not mean its runtime has initialized.
    // Match the existing engine's grace period before even querying a domain.
    sleep(6);
    json report = {{"schema", 1}, {"kind", "metadata_only"}, {"pid", getpid()},
        {"runtime_modifications_allowed", false}, {"status", "failed"}, {"classes", json::array()}};
    void* lib = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_NOW);
    if (!lib) { report["error"] = "library_not_loaded"; publish(report); return nullptr; }
#define API(ret, name, args) auto name = reinterpret_cast<ret (*) args>(dlsym(lib, "il2cpp_" #name)); if (!name) { report["error"] = "missing_export:" #name; publish(report); dlclose(lib); return nullptr; }
    API(void*, domain_get, ());
    API(void*, thread_attach, (void*));
    API(void, thread_detach, (void*));
    API(const void**, domain_get_assemblies, (void*, size_t*));
    API(void*, assembly_get_image, (const void*));
    API(const char*, image_get_name, (void*));
    API(size_t, image_get_class_count, (void*));
    API(void*, image_get_class, (void*, size_t));
    API(const char*, class_get_name, (void*));
    API(const char*, class_get_namespace, (void*));
    API(void*, class_get_parent, (void*));
    API(void*, class_get_fields, (void*, void**));
    API(void*, class_get_methods, (void*, void**));
    API(const char*, field_get_name, (void*));
    API(uint32_t, field_get_offset, (void*));
    API(int, field_get_flags, (void*));
    API(void*, field_get_type, (void*));
    API(void*, class_from_type, (void*));
    API(char*, type_get_name, (void*));
    API(void, free, (void*));
    API(const char*, method_get_name, (void*));
    API(uint32_t, method_get_param_count, (void*));
    API(void*, method_get_param, (void*, uint32_t));
    API(void*, method_get_return_type, (void*));
    API(bool, method_is_instance, (void*));
#undef API
    __android_log_print(ANDROID_LOG_INFO, "LLC_META", "metadata exports resolved; querying domain");
    void* domain = nullptr;
    for (int i = 0; i < 100 && !domain; ++i) { domain = domain_get(); if (!domain) usleep(100000); }
    if (!domain) { report["error"] = "domain_timeout"; publish(report); dlclose(lib); return nullptr; }
    // Let ordinary game startup populate the assemblies. This thread never
    // waits for or looks up a singleton, scene object, or live dictionary.
    sleep(5);
    void* thread = thread_attach(domain);
    if (!thread) { report["error"] = "attach_failed"; publish(report); dlclose(lib); return nullptr; }
    __android_log_print(ANDROID_LOG_INFO, "LLC_META", "metadata thread attached; enumerating");
    auto type_name = [&](void* type) {
        if (!type) return std::string{};
        char* raw = type_get_name(type);
        std::string name = raw ? raw : "";
        if (raw) free(raw);
        return name;
    };
    const std::unordered_set<std::string> wanted = {"GlobalGameManager", "StoryProgressManager", "StoryManager",
        "ScenarioScript", "Dialog", "DialogController", "StoryManagerUIAnimator", "TextDataSet",
        "UnitySynchronizationContext", "Awaitable", "MainThreadAwaitable", "Delegate", "SendOrPostCallback",
        "Thread", "TMP_Text", "TMP_FontAsset", "UITextDataLoader", "Dictionary`2", "List`1"};
    const std::unordered_set<std::string> methods = {"get_Instance", "ReadScenario", "ShowDialog", "Update", "GetIsTween",
        "set_text", "Post", "Invoke", "get_Current", "get_CurrentThread", "get_ManagedThreadId", "DoOnCompleted", "GetData"};
    std::vector<void*> queue;
    std::unordered_set<void*> queued;
    auto enqueue = [&](void* klass) { if (klass && queued.size() < 600 && queued.insert(klass).second) queue.push_back(klass); };
    size_t count = 0;
    const void** assemblies = domain_get_assemblies(domain, &count);
    report["assembly_count"] = count;
    if (!assemblies || count > 1000) { report["error"] = "invalid_assemblies"; }
    else {
        for (size_t i = 0; i < count; ++i) {
            void* image = assembly_get_image(assemblies[i]);
            if (!image) continue;
            size_t classes = image_get_class_count(image);
            if (classes > 200000) continue;
            for (size_t j = 0; j < classes; ++j) {
                void* k = image_get_class(image, j);
                const char* name = k ? class_get_name(k) : nullptr;
                if (name && (wanted.count(name) || !strncmp(name, "TextData_", 9) ||
                    strstr(name, "Achievement") || strstr(name, "Projection") || strstr(name, "Localize") || strstr(name, "Language"))) enqueue(k);
            }
            report["images"].push_back({{"name", image_get_name(image)}, {"classes", classes}});
        }
        for (size_t i = 0; i < queue.size(); ++i) {
            void* k = queue[i];
            json item = {{"name", class_get_name(k)}, {"namespace", class_get_namespace(k)},
                {"fields", json::array()}, {"methods", json::array()}};
            if (void* parent = class_get_parent(k)) { item["parent"] = class_get_name(parent); enqueue(parent); }
            void* iter = nullptr;
            int limit = 0;
            while (void* f = class_get_fields(k, &iter)) {
                if (++limit > 2000) break;
                auto* type = field_get_type(f);
                item["fields"].push_back({{"name", field_get_name(f)}, {"offset", field_get_offset(f)},
                    {"flags", field_get_flags(f)}, {"type", type_name(type)}});
                void* fk = type ? class_from_type(type) : nullptr;
                const char* fn = fk ? class_get_name(fk) : nullptr;
                if (fn && (!strcmp(fn, "Dictionary`2") || !strcmp(fn, "List`1"))) enqueue(fk);
            }
            iter = nullptr; limit = 0;
            while (void* m = class_get_methods(k, &iter)) {
                if (++limit > 4000) break;
                const char* name = method_get_name(m);
                if (!name || !methods.count(name)) continue;
                uint32_t argc = method_get_param_count(m);
                if (argc > 32) continue;
                json args = json::array();
                for (uint32_t n = 0; n < argc; ++n) args.push_back(type_name(method_get_param(m, n)));
                item["methods"].push_back({{"name", name}, {"instance", method_is_instance(m)},
                    {"return", type_name(method_get_return_type(m))}, {"params", args}});
            }
            report["classes"].push_back(std::move(item));
        }
        report["status"] = "collected";
        report["class_limit_reached"] = queued.size() >= 600;
    }
    thread_detach(thread);
    publish(report);
    dlclose(lib);
    return nullptr;
}
extern "C" __attribute__((visibility("default"))) void llc_core_init() {
    pthread_t thread;
    if (pthread_create(&thread, nullptr, collect, nullptr) == 0) pthread_detach(thread);
}
