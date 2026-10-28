#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <string_view>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <cstring>
#include "zygisk.hpp"

#define LOG_TAG "LLC_Zygisk"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;

static bool is_il2cpp_loaded() {
    FILE *fp = fopen("/proc/self/maps", "re");
    if (!fp) return false;
    char line[512];
    bool loaded = false;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "libil2cpp.so")) {
            loaded = true;
            break;
        }
    }
    fclose(fp);
    return loaded;
}

static void* stage2_loader_thread(void*) {
    LOGI("[LLC-STAGE1] Worker thread active (PID: %d, UID: %d). Awaiting libil2cpp.so...", getpid(), getuid());
    int waited_sec = 0;
    while (!is_il2cpp_loaded()) {
        usleep(100000); // 100ms
        waited_sec++;
        if (waited_sec % 50 == 0) {
            LOGI("[LLC-STAGE1] Awaiting libil2cpp.so (%d s elapsed)...", waited_sec / 10);
        }
        if (waited_sec > 600) { // 60s timeout
            LOGE("[LLC-STAGE1] Timeout waiting for libil2cpp.so!");
            return nullptr;
        }
    }
    LOGI("[LLC-STAGE1] libil2cpp.so successfully detected in process memory!");
    // Short grace delay to ensure IL2CPP startup and exports are ready
    usleep(500000); // 500ms

    const char* app_cache_path = "/data/data/com.ProjectMoon.LimbusCompany/cache/libllc_core.so";
    const char* app_user_cache_path = "/data/user/0/com.ProjectMoon.LimbusCompany/cache/libllc_core.so";
    const char* hot_path = "/data/local/tmp/llc/libllc_core.so";
    const char* mod_path = "/data/adb/modules/llc_zygisk/libllc_core.so";

    void* handle = nullptr;
    const char* candidate_paths[] = { app_cache_path, app_user_cache_path, hot_path, mod_path };
    for (const char* p : candidate_paths) {
        if (access(p, R_OK) == 0) {
            handle = dlopen(p, RTLD_NOW);
            if (handle) {
                LOGI("[LLC-STAGE1] libllc_core.so successfully loaded from: %s (handle: %p)", p, handle);
                break;
            } else {
                LOGW("[LLC-STAGE1] dlopen on %s failed: %s", p, dlerror());
            }
        }
    }

    if (!handle) {
        LOGE("[LLC-STAGE1] All dlopen attempts failed!");
        return nullptr;
    }

    LOGI("[LLC-STAGE1] libllc_core.so loaded successfully! Handle: %p", handle);
    typedef void (*init_func_t)();
    init_func_t core_init = (init_func_t)dlsym(handle, "llc_core_init");
    if (core_init) {
        LOGI("[LLC-STAGE1] Invoking llc_core_init()...");
        core_init();
    } else {
        LOGE("[LLC-STAGE1] Symbol llc_core_init not found: %s", dlerror());
    }
    return nullptr;
}

class LLCModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        const char *process = env->GetStringUTFChars(args->nice_name, nullptr);
        if (process) {
            if (std::string_view(process) == "com.ProjectMoon.LimbusCompany") {
                is_target = true;
                LOGI("==================================================");
                LOGI("[LLC-STAGE1] Target process identified: %s (PID: %d)", process, getpid());
                LOGI("==================================================");
            } else {
                api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            }
            env->ReleaseStringUTFChars(args->nice_name, process);
        } else {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
        }
    }

    void postAppSpecialize([[maybe_unused]] const AppSpecializeArgs *args) override {
        if (!is_target) return;
        LOGI("[LLC-STAGE1] PostAppSpecialize active (UID: %d, PID: %d). Spawning Stage 2 loader...", getuid(), getpid());
        pthread_t tid;
        int ret = pthread_create(&tid, nullptr, stage2_loader_thread, nullptr);
        if (ret == 0) {
            pthread_detach(tid);
        } else {
            LOGE("[LLC-STAGE1] Failed to create stage 2 loader thread: %d", ret);
        }
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
    bool is_target = false;
};

REGISTER_ZYGISK_MODULE(LLCModule)
