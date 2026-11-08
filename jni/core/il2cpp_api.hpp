#pragma once
#include <dlfcn.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <android/log.h>

#define LOG_TAG_CORE "LLC_CORE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG_CORE, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG_CORE, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG_CORE, __VA_ARGS__)

struct Il2CppDomain;
struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppClass;
struct Il2CppType;
struct MethodInfo {
    void* methodPointer;
};
struct Il2CppObject;
struct Il2CppString;
struct Il2CppException;
struct Il2CppArray;

typedef Il2CppDomain* (*t_il2cpp_init)(const char* name);
typedef Il2CppDomain* (*t_il2cpp_domain_get)();
typedef void* (*t_il2cpp_thread_attach)(Il2CppDomain* domain);
typedef void (*t_il2cpp_thread_detach)(void* thread);
typedef void (*t_il2cpp_shutdown)();
typedef const Il2CppAssembly** (*t_il2cpp_domain_get_assemblies)(const Il2CppDomain* domain, size_t* size);
typedef const Il2CppImage* (*t_il2cpp_assembly_get_image)(const Il2CppAssembly* assembly);
typedef const char* (*t_il2cpp_image_get_name)(const Il2CppImage* image);
typedef Il2CppClass* (*t_il2cpp_class_from_name)(const Il2CppImage* image, const char* namespaze, const char* name);
typedef const MethodInfo* (*t_il2cpp_class_get_method_from_name)(Il2CppClass* klass, const char* name, int argsCount);
typedef Il2CppObject* (*t_il2cpp_runtime_invoke)(const MethodInfo* method, void* obj, void** params, Il2CppException** exc);
typedef Il2CppString* (*t_il2cpp_string_new)(const char* str);
typedef const uint16_t* (*t_il2cpp_string_chars)(Il2CppString* str);
typedef int32_t (*t_il2cpp_string_length)(Il2CppString* str);
typedef int (*t_il2cpp_field_get_flags)(void* field);
typedef void (*t_il2cpp_gc_wbarrier_set_field)(Il2CppObject* object, void** address, void* value);
typedef void* (*t_il2cpp_resolve_icall)(const char* name);
typedef uintptr_t (*t_il2cpp_array_length)(void* array);
// Match the pointer-sized ABI verified by the bridge on supported builds.
// Narrowing a live handle to uint32_t can invalidate it.
typedef uintptr_t (*t_il2cpp_gchandle_new)(Il2CppObject*, bool);
typedef Il2CppObject* (*t_il2cpp_gchandle_get_target)(uintptr_t);
typedef void (*t_il2cpp_gchandle_free)(uintptr_t);
typedef Il2CppObject* (*t_il2cpp_object_new)(Il2CppClass* klass);
typedef Il2CppClass* (*t_il2cpp_object_get_class)(Il2CppObject* obj);
typedef void* (*t_il2cpp_object_unbox)(Il2CppObject* obj);
typedef const char* (*t_il2cpp_class_get_name)(Il2CppClass* klass);
typedef const char* (*t_il2cpp_class_get_namespace)(Il2CppClass* klass);
typedef Il2CppClass* (*t_il2cpp_class_get_parent)(Il2CppClass* klass);
typedef bool (*t_il2cpp_class_is_valuetype)(const Il2CppClass* klass);
typedef const MethodInfo* (*t_il2cpp_class_get_methods)(Il2CppClass* klass, void** iter);
typedef const char* (*t_il2cpp_method_get_name)(const MethodInfo* method);
typedef uint32_t (*t_il2cpp_method_get_param_count)(const MethodInfo* method);
typedef const Il2CppType* (*t_il2cpp_method_get_param)(const MethodInfo* method, uint32_t index);
typedef const Il2CppType* (*t_il2cpp_method_get_return_type)(const MethodInfo* method);
typedef const char* (*t_il2cpp_method_get_param_name)(const MethodInfo* method, uint32_t index);
typedef bool (*t_il2cpp_method_is_instance)(const MethodInfo* method);
typedef void* (*t_il2cpp_class_get_field_from_name)(Il2CppClass* klass, const char* name);
typedef void* (*t_il2cpp_class_get_fields)(Il2CppClass* klass, void** iter);
typedef const char* (*t_il2cpp_field_get_name)(void* field);
typedef uint32_t (*t_il2cpp_field_get_offset)(void* field);
typedef const Il2CppType* (*t_il2cpp_field_get_type)(void* field);
typedef Il2CppClass* (*t_il2cpp_class_from_type)(const Il2CppType* type);
typedef void (*t_il2cpp_field_get_value)(Il2CppObject* obj, void* field, void* value);
typedef void (*t_il2cpp_field_set_value)(Il2CppObject* obj, void* field, void* value);
typedef void (*t_il2cpp_field_static_get_value)(void* field, void* value);
typedef void (*t_il2cpp_field_static_set_value)(void* field, void* value);

typedef Il2CppObject* (*t_il2cpp_type_get_object)(const Il2CppType* type);
typedef Il2CppArray* (*t_il2cpp_array_new)(Il2CppClass* elementTypeInfo, uintptr_t length);
typedef Il2CppClass* (*t_il2cpp_class_from_system_type)(Il2CppObject* type);
typedef size_t (*t_il2cpp_image_get_class_count)(const Il2CppImage* image);
typedef void (*t_il2cpp_class_for_each)(void(*)(Il2CppClass*, void*), void*);
typedef void (*t_il2cpp_gc_foreach_heap)(void(*)(void*, void*), void*);
typedef Il2CppClass* (*t_il2cpp_image_get_class)(const Il2CppImage* image, size_t index);
typedef const Il2CppType* (*t_il2cpp_class_get_type)(Il2CppClass* klass);

inline t_il2cpp_init il2cpp_init_fn = nullptr;
inline t_il2cpp_domain_get il2cpp_domain_get_fn = nullptr;
inline t_il2cpp_thread_attach il2cpp_thread_attach_fn = nullptr;
inline t_il2cpp_thread_detach il2cpp_thread_detach_fn = nullptr;
inline t_il2cpp_shutdown il2cpp_shutdown_fn = nullptr;
inline t_il2cpp_domain_get_assemblies il2cpp_domain_get_assemblies_fn = nullptr;
inline t_il2cpp_assembly_get_image il2cpp_assembly_get_image_fn = nullptr;
inline t_il2cpp_image_get_name il2cpp_image_get_name_fn = nullptr;
inline t_il2cpp_class_from_name il2cpp_class_from_name_fn = nullptr;
inline t_il2cpp_class_get_method_from_name il2cpp_class_get_method_from_name_fn = nullptr;
inline t_il2cpp_runtime_invoke il2cpp_runtime_invoke_fn = nullptr;
inline t_il2cpp_string_new il2cpp_string_new_fn = nullptr;
inline t_il2cpp_string_chars il2cpp_string_chars_fn = nullptr;
inline t_il2cpp_string_length il2cpp_string_length_fn = nullptr;
inline t_il2cpp_field_get_flags il2cpp_field_get_flags_fn = nullptr;
inline t_il2cpp_gc_wbarrier_set_field il2cpp_gc_wbarrier_set_field_fn = nullptr;
inline t_il2cpp_resolve_icall il2cpp_resolve_icall_fn = nullptr;
inline t_il2cpp_array_length il2cpp_array_length_fn = nullptr;
inline t_il2cpp_gchandle_new il2cpp_gchandle_new_fn = nullptr;
inline t_il2cpp_gchandle_get_target il2cpp_gchandle_get_target_fn = nullptr;
inline t_il2cpp_gchandle_free il2cpp_gchandle_free_fn = nullptr;
inline t_il2cpp_object_new il2cpp_object_new_fn = nullptr;
inline t_il2cpp_object_get_class il2cpp_object_get_class_fn = nullptr;
inline t_il2cpp_object_unbox il2cpp_object_unbox_fn = nullptr;
inline t_il2cpp_class_get_name il2cpp_class_get_name_fn = nullptr;
inline t_il2cpp_class_get_namespace il2cpp_class_get_namespace_fn = nullptr;
inline t_il2cpp_class_get_parent il2cpp_class_get_parent_fn = nullptr;
inline t_il2cpp_class_is_valuetype il2cpp_class_is_valuetype_fn = nullptr;
inline t_il2cpp_class_get_methods il2cpp_class_get_methods_fn = nullptr;
inline t_il2cpp_method_get_name il2cpp_method_get_name_fn = nullptr;
inline t_il2cpp_method_get_param_count il2cpp_method_get_param_count_fn = nullptr;
inline t_il2cpp_method_get_param il2cpp_method_get_param_fn = nullptr;
inline t_il2cpp_method_get_return_type il2cpp_method_get_return_type_fn = nullptr;
inline t_il2cpp_method_get_param_name il2cpp_method_get_param_name_fn = nullptr;
inline t_il2cpp_method_is_instance il2cpp_method_is_instance_fn = nullptr;
inline t_il2cpp_class_get_field_from_name il2cpp_class_get_field_from_name_fn = nullptr;
inline t_il2cpp_class_get_fields il2cpp_class_get_fields_fn = nullptr;
inline t_il2cpp_field_get_name il2cpp_field_get_name_fn = nullptr;
inline t_il2cpp_field_get_offset il2cpp_field_get_offset_fn = nullptr;
inline t_il2cpp_field_get_type il2cpp_field_get_type_fn = nullptr;
inline t_il2cpp_class_from_type il2cpp_class_from_type_fn = nullptr;
inline t_il2cpp_field_get_value il2cpp_field_get_value_fn = nullptr;
inline t_il2cpp_field_set_value il2cpp_field_set_value_fn = nullptr;
inline t_il2cpp_field_static_get_value il2cpp_field_static_get_value_fn = nullptr;
inline t_il2cpp_field_static_set_value il2cpp_field_static_set_value_fn = nullptr;
inline t_il2cpp_type_get_object il2cpp_type_get_object_fn = nullptr;
inline t_il2cpp_image_get_class_count il2cpp_image_get_class_count_fn = nullptr;
inline t_il2cpp_image_get_class il2cpp_image_get_class_fn = nullptr;
inline t_il2cpp_class_get_type il2cpp_class_get_type_fn = nullptr;
inline t_il2cpp_array_new il2cpp_array_new_fn = nullptr;
inline t_il2cpp_class_from_system_type il2cpp_class_from_system_type_fn = nullptr;
inline t_il2cpp_class_for_each il2cpp_class_for_each_fn = nullptr;
inline t_il2cpp_gc_foreach_heap il2cpp_gc_foreach_heap_fn = nullptr;

inline bool init_il2cpp_api() {
    void* handle = dlopen("libil2cpp.so", RTLD_NOLOAD);
    if (!handle) {
        handle = dlopen("libil2cpp.so", RTLD_NOW);
    }
    if (!handle) {
        LOGE("Failed to dlopen libil2cpp.so: %s", dlerror());
        return false;
    }

    #define RESOLVE(name) \
        name##_fn = (t_##name)dlsym(handle, #name); \
        if (!name##_fn) { LOGE("Failed to resolve symbol %s", #name); return false; }

    RESOLVE(il2cpp_init);
    RESOLVE(il2cpp_domain_get);
    RESOLVE(il2cpp_thread_attach);
    il2cpp_thread_detach_fn = reinterpret_cast<t_il2cpp_thread_detach>(dlsym(handle, "il2cpp_thread_detach"));
    il2cpp_shutdown_fn = reinterpret_cast<t_il2cpp_shutdown>(dlsym(handle, "il2cpp_shutdown"));
    RESOLVE(il2cpp_domain_get_assemblies);
    RESOLVE(il2cpp_assembly_get_image);
    RESOLVE(il2cpp_image_get_name);
    RESOLVE(il2cpp_class_from_name);
    RESOLVE(il2cpp_class_get_method_from_name);
    RESOLVE(il2cpp_runtime_invoke);
    RESOLVE(il2cpp_string_new);
    RESOLVE(il2cpp_string_chars);
    RESOLVE(il2cpp_string_length);
    RESOLVE(il2cpp_field_get_flags);
    RESOLVE(il2cpp_gc_wbarrier_set_field);
    RESOLVE(il2cpp_resolve_icall);
    RESOLVE(il2cpp_array_length);
    // Optional for typed boxed-entry traversal; unavailable exports skip that path.
    il2cpp_gchandle_new_fn = reinterpret_cast<t_il2cpp_gchandle_new>(dlsym(handle, "il2cpp_gchandle_new"));
    il2cpp_gchandle_get_target_fn = reinterpret_cast<t_il2cpp_gchandle_get_target>(dlsym(handle, "il2cpp_gchandle_get_target"));
    il2cpp_gchandle_free_fn = reinterpret_cast<t_il2cpp_gchandle_free>(dlsym(handle, "il2cpp_gchandle_free"));
    RESOLVE(il2cpp_object_new);
    RESOLVE(il2cpp_object_get_class);
    RESOLVE(il2cpp_object_unbox);
    RESOLVE(il2cpp_class_get_name);
    RESOLVE(il2cpp_class_get_namespace);
    RESOLVE(il2cpp_class_get_parent);
    RESOLVE(il2cpp_class_is_valuetype);
    RESOLVE(il2cpp_class_get_methods);
    RESOLVE(il2cpp_method_get_name);
    RESOLVE(il2cpp_method_get_param_count);
    RESOLVE(il2cpp_method_get_param);
    RESOLVE(il2cpp_method_get_return_type);
    RESOLVE(il2cpp_method_get_param_name);
    RESOLVE(il2cpp_method_is_instance);
    RESOLVE(il2cpp_class_get_field_from_name);
    RESOLVE(il2cpp_class_get_fields);
    RESOLVE(il2cpp_field_get_name);
    RESOLVE(il2cpp_field_get_offset);
    RESOLVE(il2cpp_field_get_type);
    RESOLVE(il2cpp_class_from_type);
    RESOLVE(il2cpp_field_get_value);
    RESOLVE(il2cpp_field_set_value);
    RESOLVE(il2cpp_field_static_get_value);
    RESOLVE(il2cpp_field_static_set_value);
    RESOLVE(il2cpp_type_get_object);
    RESOLVE(il2cpp_image_get_class_count);
    RESOLVE(il2cpp_image_get_class);
    RESOLVE(il2cpp_class_get_type);
    RESOLVE(il2cpp_array_new);
    RESOLVE(il2cpp_class_from_system_type);
    RESOLVE(il2cpp_class_for_each);
    RESOLVE(il2cpp_gc_foreach_heap);

    #undef RESOLVE

    LOGI("IL2CPP APIs resolved successfully.");
    return true;
}

inline Il2CppClass* find_class_any(const char* namespaze, const char* class_name) {
    Il2CppDomain* domain = il2cpp_domain_get_fn();
    if (!domain) return nullptr;
    size_t size = 0;
    const Il2CppAssembly** assemblies = il2cpp_domain_get_assemblies_fn(domain, &size);
    if (!assemblies) return nullptr;
    for (size_t i = 0; i < size; ++i) {
        const Il2CppImage* img = il2cpp_assembly_get_image_fn(assemblies[i]);
        if (img) {
            Il2CppClass* klass = il2cpp_class_from_name_fn(img, namespaze, class_name);
            if (klass) return klass;
        }
    }
    return nullptr;
}

inline Il2CppClass* find_class_by_name(const char* class_name) {
    Il2CppDomain* domain = il2cpp_domain_get_fn();
    if (!domain) return nullptr;
    size_t size = 0;
    const Il2CppAssembly** assemblies = il2cpp_domain_get_assemblies_fn(domain, &size);
    if (!assemblies) return nullptr;
    for (size_t i = 0; i < size; ++i) {
        const Il2CppImage* img = il2cpp_assembly_get_image_fn(assemblies[i]);
        if (!img || !il2cpp_image_get_class_count_fn || !il2cpp_image_get_class_fn) continue;
        size_t class_count = il2cpp_image_get_class_count_fn(img);
        for (size_t c = 0; c < class_count; ++c) {
            Il2CppClass* klass = il2cpp_image_get_class_fn(img, c);
            if (klass && il2cpp_class_get_name_fn) {
                const char* name = il2cpp_class_get_name_fn(klass);
                if (name && strcmp(name, class_name) == 0) {
                    return klass;
                }
            }
        }
    }
    return nullptr;
}

inline const MethodInfo* find_method_in_hierarchy(Il2CppClass* klass, const char* name, int args_count) {
    for (Il2CppClass* k = klass; k != nullptr; k = il2cpp_class_get_parent_fn(k)) {
        const MethodInfo* mi = il2cpp_class_get_method_from_name_fn(k, name, args_count);
        if (mi) return mi;
    }
    return nullptr;
}

inline void log_exception(const char* context, Il2CppException* exc) {
    if (!exc) return;
    Il2CppClass* exc_class = il2cpp_object_get_class_fn ? il2cpp_object_get_class_fn((Il2CppObject*)exc) : nullptr;
    const char* exc_name = (exc_class && il2cpp_class_get_name_fn) ? il2cpp_class_get_name_fn(exc_class) : "UnknownException";
    const MethodInfo* to_string_mi = find_method_in_hierarchy(exc_class, "ToString", 0);
    Il2CppException* sub_exc = nullptr;
    Il2CppObject* str_obj = to_string_mi ? il2cpp_runtime_invoke_fn(to_string_mi, exc, nullptr, &sub_exc) : nullptr;
    char buf[512] = {0};
    if (str_obj && il2cpp_string_chars_fn) {
        const uint16_t* u16 = il2cpp_string_chars_fn((Il2CppString*)str_obj);
        if (u16) {
            for (int k = 0; k < 511 && u16[k]; ++k) buf[k] = (char)u16[k];
        }
    }
    LOGE("[LLC-CORE] Exception in %s: [%s] %s", context, exc_name, buf);
}
