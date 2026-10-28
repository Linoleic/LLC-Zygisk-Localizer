#include "il2cpp_api.hpp"
#include "And64InlineHook.hpp"
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

static std::unordered_map<std::string, std::string> g_ui_map;
static Il2CppObject* g_shs_font = nullptr;
static Il2CppObject* g_shs_material = nullptr;
static Il2CppObject* g_shs_atlas = nullptr;

static Il2CppObject* g_empty_font = nullptr;
static Il2CppObject* g_empty_material = nullptr;

static Il2CppClass* g_singleton_tds_class = nullptr;
static void* g_singleton_tds_inst_field = nullptr;

static const MethodInfo* find_method_in_hierarchy(Il2CppClass* klass, const char* name, int args_count) {
    for (Il2CppClass* k = klass; k != nullptr; k = il2cpp_class_get_parent_fn(k)) {
        const MethodInfo* mi = il2cpp_class_get_method_from_name_fn(k, name, args_count);
        if (mi) return mi;
    }
    return nullptr;
}

static void log_exception(const char* context, Il2CppException* exc) {
    if (!exc) return;
    Il2CppClass* exc_class = il2cpp_object_get_class_fn((Il2CppObject*)exc);
    const char* exc_name = exc_class ? il2cpp_class_get_name_fn(exc_class) : "UnknownException";
    const MethodInfo* to_string_mi = find_method_in_hierarchy(exc_class, "ToString", 0);
    Il2CppException* sub_exc = nullptr;
    Il2CppObject* str_obj = to_string_mi ? il2cpp_runtime_invoke_fn(to_string_mi, exc, nullptr, &sub_exc) : nullptr;
    char buf[512] = {0};
    if (str_obj) {
        const uint16_t* u16 = il2cpp_string_chars_fn((Il2CppString*)str_obj);
        for (int k = 0; k < 511 && u16[k]; ++k) buf[k] = (char)u16[k];
    }
    LOGE("[LLC-CORE] Exception in %s: [%s] %s", context, exc_name, buf);
}

static void log_class_fields(Il2CppClass* klass) {
    if (!klass) return;
    void* iter = nullptr;
    void* f = nullptr;
    while ((f = il2cpp_class_get_fields_fn(klass, &iter))) {
        uint32_t offset = il2cpp_field_get_offset_fn(f);
        const char* fname = il2cpp_field_get_name_fn(f);
        const Il2CppType* f_type = il2cpp_field_get_type_fn ? il2cpp_field_get_type_fn(f) : nullptr;
        Il2CppClass* f_class = (f_type && il2cpp_class_from_type_fn) ? il2cpp_class_from_type_fn(f_type) : nullptr;
        const char* type_name = f_class ? il2cpp_class_get_name_fn(f_class) : "unknown";
        LOGI("[LLC-CORE] Class %s field: %s (offset: %u, type: %s)",
             il2cpp_class_get_name_fn(klass), fname, offset, type_name);

        if (offset == 0) {
            Il2CppString* str_val = nullptr;
            il2cpp_field_static_get_value_fn(f, &str_val);
            if (str_val) {
                char sbuf[256] = {0};
                const uint16_t* u16 = il2cpp_string_chars_fn(str_val);
                for (int k = 0; k < 255 && u16[k]; ++k) sbuf[k] = (char)u16[k];
                LOGI("[LLC-CORE]   -> Static string value: '%s'", sbuf);
            }
        }
    }
}

static void log_class_methods(Il2CppClass* klass) {
    if (!klass) return;
    void* iter = nullptr;
    const MethodInfo* mi = nullptr;
    while ((mi = il2cpp_class_get_methods_fn(klass, &iter))) {
        const char* mname = il2cpp_method_get_name_fn(mi);
        uint32_t pcount = il2cpp_method_get_param_count_fn(mi);
        std::string params_str;
        for (uint32_t p = 0; p < pcount; ++p) {
            const Il2CppType* pt = il2cpp_method_get_param_fn ? il2cpp_method_get_param_fn(mi, p) : nullptr;
            Il2CppClass* pc = (pt && il2cpp_class_from_type_fn) ? il2cpp_class_from_type_fn(pt) : nullptr;
            const char* pname = il2cpp_method_get_param_name_fn ? il2cpp_method_get_param_name_fn(mi, p) : "p";
            const char* pcname = pc ? il2cpp_class_get_name_fn(pc) : "unknown";
            if (p > 0) params_str += ", ";
            params_str += std::string(pcname) + " " + (pname ? pname : "");
        }
        LOGI("[LLC-CORE] Class %s method: %s(%s)",
             il2cpp_class_get_name_fn(klass), mname, params_str.c_str());
    }
}

static Il2CppClass* get_jdl_element_class(Il2CppObject* jdl_obj) {
    if (!jdl_obj) return nullptr;
    Il2CppClass* jdl_class = il2cpp_object_get_class_fn(jdl_obj);
    if (!jdl_class) return nullptr;
    const Il2CppType* jdl_type = il2cpp_class_get_type_fn(jdl_class);
    if (!jdl_type) return nullptr;
    Il2CppObject* jdl_type_obj = il2cpp_type_get_object_fn(jdl_type);
    if (!jdl_type_obj) return nullptr;

    Il2CppClass* type_class = find_class_any("System", "Type");
    const MethodInfo* get_gen_args_mi = type_class ? find_method_in_hierarchy(type_class, "GetGenericArguments", 0) : nullptr;
    if (!get_gen_args_mi) return nullptr;
    Il2CppException* exc = nullptr;
    Il2CppObject* args_arr = il2cpp_runtime_invoke_fn(get_gen_args_mi, jdl_type_obj, nullptr, &exc);
    if (args_arr && !exc && il2cpp_array_length_fn(args_arr) > 0) {
        Il2CppObject* elem_type_obj = ((Il2CppObject**)((char*)args_arr + sizeof(void*)*4))[0];
        if (elem_type_obj && il2cpp_class_from_system_type_fn) {
            return il2cpp_class_from_system_type_fn(elem_type_obj);
        }
    }
    return nullptr;
}



struct PersonalityEntry {
    int id = 0;
    std::string title;
    std::string name;
    std::string desc;
};
static std::unordered_map<int, PersonalityEntry> g_personalities;

struct PassiveEntry {
    int id = 0;
    std::string name;
    std::string desc;
    std::string summary;
    std::string flavor;
};
static std::unordered_map<int, PassiveEntry> g_passives;

struct BufEntry {
    std::string id;
    std::string name;
    std::string desc;
    std::string summary;
    std::string flavor;
};
static std::unordered_map<std::string, BufEntry> g_bufs;
static std::unordered_map<std::string, BufEntry> g_battle_keywords;

struct EgoEntry {
    int id = 0;
    std::string name;
    std::string desc;
};
static std::unordered_map<int, EgoEntry> g_egos;

struct CharacterEntry {
    int id = 0;
    std::string name;
};
static std::unordered_map<int, CharacterEntry> g_characters;

struct SkillCoinDesc {
    std::string desc;
    std::string summary;
};

struct SkillCoin {
    std::vector<SkillCoinDesc> coindescs;
};

struct SkillLevel {
    int level = 1;
    std::string name;
    std::string desc;
    std::string flavor;
    std::string rawDesc;
    std::string abName;
    std::vector<SkillCoin> coinlist;
};

struct SkillEntry {
    int id = 0;
    std::vector<SkillLevel> levelList;
};
static std::unordered_map<int, SkillEntry> g_skills;

struct VoiceDlgEntry {
    std::string id;
    std::string desc;
    std::string dlg;
};
static std::unordered_map<std::string, VoiceDlgEntry> g_voice_dlgs;

struct TheaterEntry {
    std::string id;
    std::string title;
    std::string desc;
};
static std::unordered_map<std::string, TheaterEntry> g_theater_map;

struct BannerEntry {
    int id = 0;
    std::string name;
    std::string desc;
    std::string flavor;
};
static std::unordered_map<int, BannerEntry> g_banner_map;

struct StageChapterEntry {
    std::string id;
    std::string company;
    std::string area;
    std::string chaptertitle;
    std::string chapter;
    std::string chapterNumber;
    std::string timeline;
};
static std::unordered_map<std::string, StageChapterEntry> g_stage_chapter_map;

struct StageNodeEntry {
    int id = 0;
    std::string title;
    std::string desc;
    std::string place;
};
static std::unordered_map<int, StageNodeEntry> g_stage_node_map;

static const char* find_json_object_end(const char* start) {
    const char* p = start;
    int depth = 0;
    bool in_string = false;
    while (*p) {
        if (*p == '\\' && in_string) {
            p += 2;
            continue;
        }
        if (*p == '\"') {
            in_string = !in_string;
        } else if (!in_string) {
            if (*p == '{') {
                depth++;
            } else if (*p == '}') {
                depth--;
                if (depth == 0) return p;
            }
        }
        p++;
    }
    return nullptr;
}

static bool extract_json_field(const char* start, const char* end, const char* key, std::string& out) {
    const char* p = strstr(start, key);
    if (!p || p >= end) return false;
    p += strlen(key);
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (p >= end || *p != '\"') return false;
    p++;
    out.clear();
    while (p < end && *p != '\"') {
        if (*p == '\\') {
            char next_c = *(p + 1);
            if (next_c == '\"') { out.push_back('\"'); p += 2; }
            else if (next_c == 'n') { out.push_back('\n'); p += 2; }
            else if (next_c == 'r') { out.push_back('\r'); p += 2; }
            else if (next_c == 't') { out.push_back('\t'); p += 2; }
            else if (next_c == '\\') { out.push_back('\\'); p += 2; }
            else { p++; out.push_back(*p++); }
        } else {
            out.push_back(*p++);
        }
    }
    return true;
}

static bool extract_json_id(const char* start, const char* end, std::string& out_id_str, int& out_id_int) {
    const char* p = strstr(start, "\"id\"");
    if (!p || p >= end) return false;
    p += 4;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (p >= end) return false;
    out_id_str.clear();
    out_id_int = 0;
    if (*p == '\"') {
        p++;
        while (p < end && *p != '\"') out_id_str.push_back(*p++);
        out_id_int = atoi(out_id_str.c_str());
        return !out_id_str.empty();
    } else {
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '-')) out_id_str.push_back(*p++);
        out_id_int = atoi(out_id_str.c_str());
        return !out_id_str.empty();
    }
}

static bool extract_json_int_named(const char* start, const char* end, const char* key, int& out) {
    const char* p = strstr(start, key);
    if (!p || p >= end) return false;
    p += strlen(key);
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
    if (p >= end) return false;
    out = atoi(p);
    return true;
}

static void parse_personality_json(const std::string& filepath) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 10 * 1024 * 1024) { fclose(fp); return; }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    const char* cur = buf;
    int count = 0;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && id_int != 0) {
            PersonalityEntry entry;
            entry.id = id_int;
            extract_json_field(cur, obj_end, "\"title\"", entry.title);
            extract_json_field(cur, obj_end, "\"name\"", entry.name);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            g_personalities[id_int] = entry;
            count++;
        }
        cur = obj_end + 1;
    }
    free(buf);
}

static void parse_passive_json(const std::string& filepath) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 10 * 1024 * 1024) { fclose(fp); return; }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    const char* cur = buf;
    int count = 0;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && id_int != 0) {
            PassiveEntry entry;
            entry.id = id_int;
            extract_json_field(cur, obj_end, "\"name\"", entry.name);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            extract_json_field(cur, obj_end, "\"summary\"", entry.summary);
            extract_json_field(cur, obj_end, "\"flavor\"", entry.flavor);
            g_passives[id_int] = entry;
            count++;
        }
        cur = obj_end + 1;
    }
    free(buf);
}

static void parse_buf_json(const std::string& filepath) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 10 * 1024 * 1024) { fclose(fp); return; }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    const char* cur = buf;
    int count = 0;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && !id_str.empty()) {
            BufEntry entry;
            entry.id = id_str;
            extract_json_field(cur, obj_end, "\"name\"", entry.name);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            extract_json_field(cur, obj_end, "\"summary\"", entry.summary);
            extract_json_field(cur, obj_end, "\"flavor\"", entry.flavor);
            g_bufs[id_str] = entry;
            count++;
        }
        cur = obj_end + 1;
    }
    free(buf);
}

static void parse_keyword_json(const std::string& filepath) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 10 * 1024 * 1024) { fclose(fp); return; }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    const char* cur = buf;
    int count = 0;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && !id_str.empty()) {
            BufEntry entry;
            entry.id = id_str;
            extract_json_field(cur, obj_end, "\"name\"", entry.name);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            extract_json_field(cur, obj_end, "\"summary\"", entry.summary);
            g_battle_keywords[id_str] = entry;
            if (g_bufs.find(id_str) == g_bufs.end()) {
                g_bufs[id_str] = entry;
            }
            count++;
        }
        cur = obj_end + 1;
    }
    free(buf);
}

static void parse_ego_json(const std::string& filepath) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 10 * 1024 * 1024) { fclose(fp); return; }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && id_int != 0) {
            EgoEntry entry;
            entry.id = id_int;
            extract_json_field(cur, obj_end, "\"name\"", entry.name);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            g_egos[id_int] = entry;
        }
        cur = obj_end + 1;
    }
    free(buf);
}

static void parse_character_json(const std::string& filepath) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 10 * 1024 * 1024) { fclose(fp); return; }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && id_int != 0) {
            CharacterEntry entry;
            entry.id = id_int;
            extract_json_field(cur, obj_end, "\"name\"", entry.name);
            g_characters[id_int] = entry;
        }
        cur = obj_end + 1;
    }
    free(buf);
}

static void parse_skill_json(const std::string& filepath) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 15 * 1024 * 1024) { fclose(fp); return; }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && id_int != 0) {
            SkillEntry entry;
            entry.id = id_int;

            const char* ll_ptr = strstr(cur, "\"levelList\"");
            if (ll_ptr && ll_ptr < obj_end) {
                const char* ll_bracket = strchr(ll_ptr, '[');
                if (ll_bracket && ll_bracket < obj_end) {
                    const char* l_cur = ll_bracket + 1;
                    while (l_cur < obj_end) {
                        while (l_cur < obj_end && *l_cur != '{' && *l_cur != ']') l_cur++;
                        if (l_cur >= obj_end || *l_cur == ']') break;

                        const char* lvl_start = l_cur;
                        const char* lvl_end = find_json_object_end(lvl_start);
                        if (!lvl_end || lvl_end > obj_end) break;

                        SkillLevel lvl;
                        extract_json_int_named(lvl_start, lvl_end, "\"level\"", lvl.level);

                        const char* cl_ptr = strstr(lvl_start, "\"coinlist\"");
                        const char* lvl_header_end = (cl_ptr && cl_ptr < lvl_end) ? cl_ptr : lvl_end;

                        extract_json_field(lvl_start, lvl_header_end, "\"name\"", lvl.name);
                        extract_json_field(lvl_start, lvl_header_end, "\"desc\"", lvl.desc);
                        extract_json_field(lvl_start, lvl_header_end, "\"flavor\"", lvl.flavor);
                        extract_json_field(lvl_start, lvl_header_end, "\"rawDesc\"", lvl.rawDesc);
                        extract_json_field(lvl_start, lvl_header_end, "\"abName\"", lvl.abName);

                        if (cl_ptr && cl_ptr < lvl_end) {
                            const char* cl_bracket = strchr(cl_ptr, '[');
                            if (cl_bracket && cl_bracket < lvl_end) {
                                const char* c_cur = cl_bracket + 1;
                                while (c_cur < lvl_end) {
                                    while (c_cur < lvl_end && *c_cur != '{' && *c_cur != ']') c_cur++;
                                    if (c_cur >= lvl_end || *c_cur == ']') break;

                                    const char* c_start = c_cur;
                                    const char* c_end = find_json_object_end(c_start);
                                    if (!c_end || c_end > lvl_end) break;

                                    SkillCoin coin;
                                    const char* cd_ptr = strstr(c_start, "\"coindescs\"");
                                    if (cd_ptr && cd_ptr < c_end) {
                                        const char* cd_bracket = strchr(cd_ptr, '[');
                                        if (cd_bracket && cd_bracket < c_end) {
                                            const char* cd_cur = cd_bracket + 1;
                                            while (cd_cur < c_end) {
                                                while (cd_cur < c_end && *cd_cur != '{' && *cd_cur != ']') cd_cur++;
                                                if (cd_cur >= c_end || *cd_cur == ']') break;

                                                const char* cd_start = cd_cur;
                                                const char* cd_end = find_json_object_end(cd_start);
                                                if (!cd_end || cd_end > c_end) break;

                                                SkillCoinDesc cd;
                                                extract_json_field(cd_start, cd_end, "\"desc\"", cd.desc);
                                                extract_json_field(cd_start, cd_end, "\"summary\"", cd.summary);
                                                coin.coindescs.push_back(cd);

                                                cd_cur = cd_end + 1;
                                            }
                                        }
                                    }
                                    lvl.coinlist.push_back(coin);
                                    c_cur = c_end + 1;
                                }
                            }
                        }
                        entry.levelList.push_back(lvl);
                        l_cur = lvl_end + 1;
                    }
                }
            }
            g_skills[id_int] = entry;
        }
        cur = obj_end + 1;
    }
    free(buf);
}

static void parse_voice_json_file(const std::string& filepath) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 5 * 1024 * 1024) { fclose(fp); return; }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int dummy_int = 0;
        if (extract_json_id(cur, obj_end, id_str, dummy_int) && !id_str.empty()) {
            VoiceDlgEntry entry;
            entry.id = id_str;
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            extract_json_field(cur, obj_end, "\"dlg\"", entry.dlg);
            extract_json_field(cur, obj_end, "\"lyrics\"", entry.dlg);
            g_voice_dlgs[id_str] = entry;
        }
        cur = obj_end + 1;
    }
    free(buf);
}

static void load_voice_dir(const char* dir_path) {
    DIR* d = opendir(dir_path);
    if (!d) return;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        const char* name = entry->d_name;
        size_t len = strlen(name);
        if (len > 5 && strcmp(name + len - 5, ".json") == 0) {
            std::string full_path = std::string(dir_path) + "/" + name;
            parse_voice_json_file(full_path);
        }
    }
    closedir(d);
}

static void parse_theater_json_file(const std::string& filepath) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 5 * 1024 * 1024) { fclose(fp); return; }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int dummy_int = 0;
        if (extract_json_id(cur, obj_end, id_str, dummy_int) && !id_str.empty()) {
            TheaterEntry entry;
            entry.id = id_str;
            extract_json_field(cur, obj_end, "\"title\"", entry.title);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            g_theater_map[id_str] = entry;
        }
        cur = obj_end + 1;
    }
    free(buf);
}

static void parse_banner_json_file(const std::string& filepath) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 5 * 1024 * 1024) { fclose(fp); return; }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && id_int != 0) {
            BannerEntry entry;
            entry.id = id_int;
            extract_json_field(cur, obj_end, "\"name\"", entry.name);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            extract_json_field(cur, obj_end, "\"flavor\"", entry.flavor);
            g_banner_map[id_int] = entry;
        }
        cur = obj_end + 1;
    }
    free(buf);
}

static void parse_stage_chapter_json_file(const std::string& filepath) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 5 * 1024 * 1024) { fclose(fp); return; }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int dummy_int = 0;
        if (extract_json_id(cur, obj_end, id_str, dummy_int) && !id_str.empty()) {
            StageChapterEntry entry;
            entry.id = id_str;
            extract_json_field(cur, obj_end, "\"company\"", entry.company);
            extract_json_field(cur, obj_end, "\"area\"", entry.area);
            extract_json_field(cur, obj_end, "\"chaptertitle\"", entry.chaptertitle);
            extract_json_field(cur, obj_end, "\"chapter\"", entry.chapter);
            extract_json_field(cur, obj_end, "\"chapterNumber\"", entry.chapterNumber);
            extract_json_field(cur, obj_end, "\"timeline\"", entry.timeline);
            g_stage_chapter_map[id_str] = entry;
        }
        cur = obj_end + 1;
    }
    free(buf);
}

static void parse_stage_node_json_file(const std::string& filepath) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 5 * 1024 * 1024) { fclose(fp); return; }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    const char* cur = buf;
    while ((cur = strstr(cur, "\"id\""))) {
        const char* obj_start = cur;
        while (obj_start > buf && *obj_start != '{') obj_start--;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        std::string id_str;
        int id_int = 0;
        if (extract_json_id(cur, obj_end, id_str, id_int) && id_int != 0) {
            StageNodeEntry entry;
            entry.id = id_int;
            extract_json_field(cur, obj_end, "\"title\"", entry.title);
            extract_json_field(cur, obj_end, "\"desc\"", entry.desc);
            extract_json_field(cur, obj_end, "\"place\"", entry.place);
            g_stage_node_map[id_int] = entry;
        }
        cur = obj_end + 1;
    }
    free(buf);
}

static void load_all_content_jsons(const char* dir_path) {
    DIR* d = opendir(dir_path);
    if (!d) return;
    struct dirent* entry;
    int pass_f = 0, buf_f = 0, kw_f = 0, ego_f = 0, pers_f = 0, skill_f = 0, char_f = 0;
    while ((entry = readdir(d)) != nullptr) {
        const char* name = entry->d_name;
        size_t len = strlen(name);
        if (len <= 5 || strcmp(name + len - 5, ".json") != 0) continue;
        std::string full_path = std::string(dir_path) + "/" + name;

        if (strncmp(name, "Personalities", 13) == 0) {
            parse_personality_json(full_path);
            pers_f++;
        } else if (strncmp(name, "Passive", 7) == 0) {
            parse_passive_json(full_path);
            pass_f++;
        } else if (strncmp(name, "Bufs", 4) == 0) {
            parse_buf_json(full_path);
            buf_f++;
        } else if (strncmp(name, "BattleKeywords", 14) == 0) {
            parse_keyword_json(full_path);
            kw_f++;
        } else if (strncmp(name, "Egos", 4) == 0) {
            parse_ego_json(full_path);
            ego_f++;
        } else if (strncmp(name, "Skills", 6) == 0) {
            parse_skill_json(full_path);
            skill_f++;
        } else if (strcmp(name, "Characters.json") == 0) {
            parse_character_json(full_path);
            char_f++;
        } else if (strcmp(name, "UserBanner.json") == 0) {
            parse_banner_json_file(full_path);
        } else if (strncmp(name, "StoryTheater", 12) == 0) {
            parse_theater_json_file(full_path);
        } else if (strncmp(name, "StageChapterText", 16) == 0) {
            parse_stage_chapter_json_file(full_path);
        } else if (strncmp(name, "StageNode", 9) == 0 || strncmp(name, "RailwayDungeon", 14) == 0) {
            parse_stage_node_json_file(full_path);
        }
    }
    closedir(d);

    // Load subdirectories for voice lines
    std::string base_dir(dir_path);
    load_voice_dir((base_dir + "/PersonalityVoiceDlg").c_str());
    load_voice_dir((base_dir + "/BattleAnnouncerDlg").c_str());
    load_voice_dir((base_dir + "/EGOVoiceDig").c_str());
    load_voice_dir((base_dir + "/BgmLyrics").c_str());

    LOGI("[LLC-CORE] Content JSONs loaded: %zu pers, %zu pass, %zu bufs, %zu kws, %zu egos, %zu skills, %zu chars, %zu voices, %zu banners, %zu theaters, %zu chapters, %zu nodes",
         g_personalities.size(), g_passives.size(), g_bufs.size(), g_battle_keywords.size(),
         g_egos.size(), g_skills.size(), g_characters.size(), g_voice_dlgs.size(),
         g_banner_map.size(), g_theater_map.size(), g_stage_chapter_map.size(), g_stage_node_map.size());
}



static int patch_personality_list(Il2CppObject* pers_list_obj) {
    if (!pers_list_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)pers_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        int id = *(int*)((char*)v + 16);
        int key_int = *(int*)(entry_ptr + 8);
        if (id == 0 && key_int != 0) id = key_int;

        if (g_personalities.find(id) == g_personalities.end()) {
            Il2CppString* k_str = *(Il2CppString**)(entry_ptr + 8);
            if (k_str && (uintptr_t)k_str > 0x10000) {
                const uint16_t* u = il2cpp_string_chars_fn(k_str);
                char kbuf[32] = {0};
                for (int x = 0; x < 31 && u[x]; ++x) kbuf[x] = (char)u[x];
                int parsed_id = atoi(kbuf);
                if (parsed_id != 0) id = parsed_id;
            }
        }

        auto it = g_personalities.find(id);
        if (it != g_personalities.end()) {
            if (!it->second.name.empty()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.name.c_str());
            }
            if (!it->second.desc.empty()) {
                *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.desc.c_str());
            }
            if (!it->second.title.empty()) {
                *(Il2CppString**)((char*)v + 40) = il2cpp_string_new_fn(it->second.title.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-CORE] _personalityList patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

static int patch_passive_list(Il2CppObject* passive_list_obj) {
    if (!passive_list_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)passive_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        int id = 0;
        Il2CppString* id_str = *(Il2CppString**)((char*)v + 16);
        if (id_str && (uintptr_t)id_str > 0x10000) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            char buf[32] = {0};
            for (int x = 0; x < 31 && u[x]; ++x) buf[x] = (char)u[x];
            id = atoi(buf);
        }
        if (id == 0) {
            Il2CppString* k_str = *(Il2CppString**)(entry_ptr + 8);
            if (k_str && (uintptr_t)k_str > 0x10000) {
                const uint16_t* u = il2cpp_string_chars_fn(k_str);
                char buf[32] = {0};
                for (int x = 0; x < 31 && u[x]; ++x) buf[x] = (char)u[x];
                id = atoi(buf);
            }
        }
        if (id == 0) {
            id = *(int*)(entry_ptr + 8);
        }

        auto it = g_passives.find(id);
        if (it != g_passives.end()) {
            if (!it->second.name.empty()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.name.c_str());
            }
            if (!it->second.desc.empty()) {
                *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.desc.c_str());
            }
            if (!it->second.summary.empty()) {
                *(Il2CppString**)((char*)v + 40) = il2cpp_string_new_fn(it->second.summary.c_str());
            }
            if (!it->second.flavor.empty()) {
                *(Il2CppString**)((char*)v + 48) = il2cpp_string_new_fn(it->second.flavor.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-CORE] _passiveList patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

static int patch_buf_list(Il2CppObject* buf_list_obj, const char* name_prefix) {
    if (!buf_list_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)buf_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        std::string id_str_val;
        Il2CppString* id_str = *(Il2CppString**)((char*)v + 16);
        if (id_str && (uintptr_t)id_str > 0x10000) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            for (int x = 0; x < 127 && u[x]; ++x) id_str_val.push_back((char)u[x]);
        }
        if (id_str_val.empty()) {
            Il2CppString* k_str = *(Il2CppString**)(entry_ptr + 8);
            if (k_str && (uintptr_t)k_str > 0x10000) {
                const uint16_t* u = il2cpp_string_chars_fn(k_str);
                for (int x = 0; x < 127 && u[x]; ++x) id_str_val.push_back((char)u[x]);
            }
        }

        auto it = g_bufs.find(id_str_val);
        if (it != g_bufs.end()) {
            if (!it->second.name.empty()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.name.c_str());
            }
            if (!it->second.desc.empty()) {
                *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.desc.c_str());
            }
            if (!it->second.summary.empty()) {
                *(Il2CppString**)((char*)v + 40) = il2cpp_string_new_fn(it->second.summary.c_str());
            }
            if (!it->second.flavor.empty()) {
                *(Il2CppString**)((char*)v + 48) = il2cpp_string_new_fn(it->second.flavor.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-CORE] %s patched: %d / %d entries updated with Chinese translations!", name_prefix, patched, count);
    return patched;
}

static int patch_ego_list(Il2CppObject* ego_list_obj) {
    if (!ego_list_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)ego_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        int id = 0;
        Il2CppString* id_str = *(Il2CppString**)((char*)v + 16);
        if (id_str && (uintptr_t)id_str > 0x10000) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            char buf[32] = {0};
            for (int x = 0; x < 31 && u[x]; ++x) buf[x] = (char)u[x];
            id = atoi(buf);
        }
        if (id == 0) {
            Il2CppString* k_str = *(Il2CppString**)(entry_ptr + 8);
            if (k_str && (uintptr_t)k_str > 0x10000) {
                const uint16_t* u = il2cpp_string_chars_fn(k_str);
                char buf[32] = {0};
                for (int x = 0; x < 31 && u[x]; ++x) buf[x] = (char)u[x];
                id = atoi(buf);
            }
        }
        if (id == 0) {
            id = *(int*)(entry_ptr + 8);
        }

        auto it = g_egos.find(id);
        if (it != g_egos.end()) {
            if (!it->second.name.empty()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.name.c_str());
            }
            if (!it->second.desc.empty()) {
                *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.desc.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-CORE] _egoList patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

static int patch_character_list(Il2CppObject* char_list_obj) {
    if (!char_list_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)char_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        int id = 0;
        Il2CppString* id_str = *(Il2CppString**)((char*)v + 16);
        if (id_str && (uintptr_t)id_str > 0x10000) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            char buf[32] = {0};
            for (int x = 0; x < 31 && u[x]; ++x) buf[x] = (char)u[x];
            id = atoi(buf);
        }
        if (id == 0) {
            id = *(int*)(entry_ptr + 8);
        }

        auto it = g_characters.find(id);
        if (it != g_characters.end()) {
            if (!it->second.name.empty()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.name.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-CORE] _characterList patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

static int patch_skill_list(Il2CppObject* skill_list_obj) {
    if (!skill_list_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)skill_list_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        int id = 0;
        Il2CppString* id_str = *(Il2CppString**)((char*)v + 16);
        if (id_str && (uintptr_t)id_str > 0x10000) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            char buf[32] = {0};
            for (int x = 0; x < 31 && u[x]; ++x) buf[x] = (char)u[x];
            id = atoi(buf);
        }
        if (id == 0) {
            Il2CppString* k_str = *(Il2CppString**)(entry_ptr + 8);
            if (k_str && (uintptr_t)k_str > 0x10000) {
                const uint16_t* u = il2cpp_string_chars_fn(k_str);
                char buf[32] = {0};
                for (int x = 0; x < 31 && u[x]; ++x) buf[x] = (char)u[x];
                id = atoi(buf);
            }
        }
        if (id == 0) {
            id = *(int*)(entry_ptr + 8);
        }

        auto it = g_skills.find(id);
        if (it != g_skills.end()) {
            Il2CppObject* level_list = *(Il2CppObject**)((char*)v + 24);
            if (level_list) {
                Il2CppObject* ll_items = *(Il2CppObject**)((char*)level_list + 16);
                int ll_size = *(int*)((char*)level_list + 24);
                if (ll_items && ll_size > 0) {
                    Il2CppObject** lvl_arr = (Il2CppObject**)((char*)ll_items + sizeof(void*)*4);
                    for (int l = 0; l < ll_size && l < (int)it->second.levelList.size(); ++l) {
                        Il2CppObject* lvl_obj = lvl_arr[l];
                        if (!lvl_obj) continue;
                        const auto& s_lvl = it->second.levelList[l];

                        if (!s_lvl.name.empty()) {
                            *(Il2CppString**)((char*)lvl_obj + 40) = il2cpp_string_new_fn(s_lvl.name.c_str());
                        }
                        if (!s_lvl.desc.empty()) {
                            *(Il2CppString**)((char*)lvl_obj + 32) = il2cpp_string_new_fn(s_lvl.desc.c_str());
                        }
                        if (!s_lvl.flavor.empty()) {
                            *(Il2CppString**)((char*)lvl_obj + 48) = il2cpp_string_new_fn(s_lvl.flavor.c_str());
                        }
                        if (!s_lvl.rawDesc.empty()) {
                            *(Il2CppString**)((char*)lvl_obj + 56) = il2cpp_string_new_fn(s_lvl.rawDesc.c_str());
                        }
                        if (!s_lvl.abName.empty()) {
                            *(Il2CppString**)((char*)lvl_obj + 64) = il2cpp_string_new_fn(s_lvl.abName.c_str());
                        }

                        Il2CppObject* coinlist = *(Il2CppObject**)((char*)lvl_obj + 72);
                        if (coinlist) {
                            Il2CppObject* cl_items = *(Il2CppObject**)((char*)coinlist + 16);
                            int cl_size = *(int*)((char*)coinlist + 24);
                            if (cl_items && cl_size > 0) {
                                Il2CppObject** cl_arr = (Il2CppObject**)((char*)cl_items + sizeof(void*)*4);
                                for (int c = 0; c < cl_size && c < (int)s_lvl.coinlist.size(); ++c) {
                                    Il2CppObject* coin_obj = cl_arr[c];
                                    if (!coin_obj) continue;
                                    const auto& s_coin = s_lvl.coinlist[c];

                                    Il2CppObject* coindescs = *(Il2CppObject**)((char*)coin_obj + 16);
                                    if (coindescs) {
                                        Il2CppObject* cd_items = *(Il2CppObject**)((char*)coindescs + 16);
                                        int cd_size = *(int*)((char*)coindescs + 24);
                                        if (cd_items && cd_size > 0) {
                                            Il2CppObject** cd_arr = (Il2CppObject**)((char*)cd_items + sizeof(void*)*4);
                                            for (int d = 0; d < cd_size && d < (int)s_coin.coindescs.size(); ++d) {
                                                Il2CppObject* cd_obj = cd_arr[d];
                                                if (!cd_obj) continue;
                                                const auto& s_cd = s_coin.coindescs[d];

                                                if (!s_cd.desc.empty()) {
                                                    *(Il2CppString**)((char*)cd_obj + 16) = il2cpp_string_new_fn(s_cd.desc.c_str());
                                                }
                                                if (!s_cd.summary.empty()) {
                                                    *(Il2CppString**)((char*)cd_obj + 24) = il2cpp_string_new_fn(s_cd.summary.c_str());
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            patched++;
        }
    }
    LOGI("[LLC-CORE] _skillList patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

static void parse_ui_json_file(const std::string& filepath) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 15 * 1024 * 1024) { fclose(fp); return; }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    const char* p = buf;
    while ((p = strstr(p, "\"id\""))) {
        p += 4;
        while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
        if (*p != '\"') continue;
        p++;
        std::string id_str;
        while (*p && *p != '\"') {
            if (*p == '\\' && *(p+1)) {
                p++;
                id_str.push_back(*p++);
            } else {
                id_str.push_back(*p++);
            }
        }
        if (*p == '\"') p++;

        const char* c_tag = strstr(p, "\"content\"");
        const char* next_id = strstr(p, "\"id\"");
        if (!c_tag || (next_id && c_tag > next_id)) {
            continue;
        }

        p = c_tag + 9;
        while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':')) p++;
        if (*p != '\"') continue;
        p++;
        std::string content_str;
        while (*p && *p != '\"') {
            if (*p == '\\') {
                char next_c = *(p + 1);
                if (next_c == '\"') { content_str.push_back('\"'); p += 2; }
                else if (next_c == 'n') { content_str.push_back('\n'); p += 2; }
                else if (next_c == 'r') { content_str.push_back('\r'); p += 2; }
                else if (next_c == 't') { content_str.push_back('\t'); p += 2; }
                else if (next_c == '\\') { content_str.push_back('\\'); p += 2; }
                else { p++; content_str.push_back(*p++); }
            } else {
                content_str.push_back(*p++);
            }
        }
        if (*p == '\"') p++;

        if (!id_str.empty()) {
            g_ui_map[id_str] = content_str;
        }
    }
    free(buf);
}

static void load_all_ui_jsons(const char* dir_path) {
    DIR* d = opendir(dir_path);
    if (!d) {
        LOGE("[LLC-CORE] Failed to open lang dir: %s", dir_path);
        return;
    }
    struct dirent* entry;
    int files_loaded = 0;
    while ((entry = readdir(d)) != nullptr) {
        const char* name = entry->d_name;
        size_t len = strlen(name);
        if (len > 5 && strcmp(name + len - 5, ".json") == 0) {
            std::string full_path = std::string(dir_path) + "/" + name;
            parse_ui_json_file(full_path);
            files_loaded++;
        }
    }
    closedir(d);
    LOGI("[LLC-CORE] Loaded %d JSON files, total %zu unique UI keys in g_ui_map", files_loaded, g_ui_map.size());
}

static int patch_voice_list(Il2CppObject* voice_container_obj, const char* name) {
    if (!voice_container_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)voice_container_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        // v is LocalizeTextDataRoot<TextData_PersonalityVoice>
        Il2CppObject* dlist = *(Il2CppObject**)((char*)v + 16);
        if (!dlist) continue;

        Il2CppObject* ditems = *(Il2CppObject**)((char*)dlist + 16);
        int dsize = *(int*)((char*)dlist + 24);
        if (!ditems || dsize <= 0) continue;

        Il2CppObject** arr = (Il2CppObject**)((char*)ditems + sizeof(void*) * 4);
        for (int j = 0; j < dsize; ++j) {
            Il2CppObject* item = arr[j];
            if (!item) continue;

            Il2CppString* id_str = *(Il2CppString**)((char*)item + 16);
            if (!id_str || (uintptr_t)id_str <= 0x10000) continue;

            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            char id_buf[128] = {0};
            for (int x = 0; x < 127 && u[x]; ++x) id_buf[x] = (char)u[x];

            auto it = g_voice_dlgs.find(id_buf);
            if (it != g_voice_dlgs.end()) {
                if (!it->second.desc.empty()) {
                    *(Il2CppString**)((char*)item + 24) = il2cpp_string_new_fn(it->second.desc.c_str());
                }
                if (!it->second.dlg.empty()) {
                    *(Il2CppString**)((char*)item + 32) = il2cpp_string_new_fn(it->second.dlg.c_str());
                }
                patched++;
            }
        }
    }
    LOGI("[LLC-CORE] %s patched: %d voice lines updated with Chinese translations!", name, patched);
    return patched;
}

static int patch_theater_list(Il2CppObject* theater_obj) {
    if (!theater_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)theater_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        char id_buf[64] = {0};
        Il2CppString* id_str = *(Il2CppString**)((char*)v + 16);
        if (id_str && (uintptr_t)id_str > 0x10000) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            for (int x = 0; x < 63 && u[x]; ++x) id_buf[x] = (char)u[x];
        }
        if (id_buf[0] == '\0') {
            Il2CppString* k_str = *(Il2CppString**)(entry_ptr + 8);
            if (k_str && (uintptr_t)k_str > 0x10000) {
                const uint16_t* u = il2cpp_string_chars_fn(k_str);
                for (int x = 0; x < 63 && u[x]; ++x) id_buf[x] = (char)u[x];
            }
        }
        if (id_buf[0] == '\0') {
            int kid = *(int*)(entry_ptr + 8);
            snprintf(id_buf, sizeof(id_buf), "%d", kid);
        }

        auto it = g_theater_map.find(id_buf);
        if (it != g_theater_map.end()) {
            if (!it->second.title.empty()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.title.c_str());
            }
            if (!it->second.desc.empty()) {
                *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.desc.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-CORE] _storyTheater patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

static int patch_banner_list(Il2CppObject* banner_obj) {
    if (!banner_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)banner_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        int id = *(int*)((char*)v + 16);
        if (id == 0) id = *(int*)(entry_ptr + 8);

        auto it = g_banner_map.find(id);
        if (it != g_banner_map.end()) {
            if (!it->second.name.empty()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.name.c_str());
            }
            if (!it->second.desc.empty()) {
                *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.desc.c_str());
            }
            if (!it->second.flavor.empty()) {
                *(Il2CppString**)((char*)v + 40) = il2cpp_string_new_fn(it->second.flavor.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-CORE] _userBanner patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

static int patch_stage_chapter_list(Il2CppObject* chapter_obj) {
    if (!chapter_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)chapter_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        char id_buf[64] = {0};
        Il2CppString* id_str = *(Il2CppString**)((char*)v + 16);
        if (id_str && (uintptr_t)id_str > 0x10000) {
            const uint16_t* u = il2cpp_string_chars_fn(id_str);
            for (int x = 0; x < 63 && u[x]; ++x) id_buf[x] = (char)u[x];
        }
        if (id_buf[0] == '\0') {
            Il2CppString* k_str = *(Il2CppString**)(entry_ptr + 8);
            if (k_str && (uintptr_t)k_str > 0x10000) {
                const uint16_t* u = il2cpp_string_chars_fn(k_str);
                for (int x = 0; x < 63 && u[x]; ++x) id_buf[x] = (char)u[x];
            }
        }
        if (id_buf[0] == '\0') {
            int kid = *(int*)(entry_ptr + 8);
            snprintf(id_buf, sizeof(id_buf), "%d", kid);
        }

        auto it = g_stage_chapter_map.find(id_buf);
        if (it != g_stage_chapter_map.end()) {
            if (!it->second.company.empty()) *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.company.c_str());
            if (!it->second.area.empty()) *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.area.c_str());
            if (!it->second.chaptertitle.empty()) *(Il2CppString**)((char*)v + 40) = il2cpp_string_new_fn(it->second.chaptertitle.c_str());
            if (!it->second.chapter.empty()) *(Il2CppString**)((char*)v + 48) = il2cpp_string_new_fn(it->second.chapter.c_str());
            if (!it->second.chapterNumber.empty()) *(Il2CppString**)((char*)v + 56) = il2cpp_string_new_fn(it->second.chapterNumber.c_str());
            if (!it->second.timeline.empty()) *(Il2CppString**)((char*)v + 64) = il2cpp_string_new_fn(it->second.timeline.c_str());
            patched++;
        }
    }
    LOGI("[LLC-CORE] _stageChapter patched: %d / %d entries updated with Chinese translations!", patched, count);
    return patched;
}

static int patch_stage_node_list(Il2CppObject* node_obj, const char* name) {
    if (!node_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)node_obj + 16);
    if (!dic) return 0;

    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    if (!entries || count <= 0) return 0;

    int patched = 0;
    char* items_base = (char*)entries + sizeof(void*) * 4;
    for (int e = 0; e < count; ++e) {
        char* entry_ptr = items_base + e * 24;
        Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
        if (!v) continue;

        int id = *(int*)((char*)v + 16);
        if (id == 0) id = *(int*)(entry_ptr + 8);

        auto it = g_stage_node_map.find(id);
        if (it != g_stage_node_map.end()) {
            if (!it->second.title.empty()) *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.title.c_str());
            if (!it->second.desc.empty()) *(Il2CppString**)((char*)v + 32) = il2cpp_string_new_fn(it->second.desc.c_str());
            if (!it->second.place.empty()) *(Il2CppString**)((char*)v + 40) = il2cpp_string_new_fn(it->second.place.c_str());
            patched++;
        }
    }
    LOGI("[LLC-CORE] %s patched: %d / %d entries updated with Chinese translations!", name, patched, count);
    return patched;
}

struct ScenarioDialogue {
    int id = 0;
    std::string content;
    std::string place;
    std::string teller;
    std::string title;
};

static void parse_scenario_json_file(const std::string& path, std::unordered_map<int, ScenarioDialogue>& out) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 5 * 1024 * 1024) { fclose(fp); return; }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    const char* p = buf;
    while (*p) {
        const char* obj_start = strchr(p, '{');
        if (!obj_start) break;
        const char* obj_end = find_json_object_end(obj_start);
        if (!obj_end) break;

        const char* id_field = strstr(obj_start, "\"id\"");
        if (id_field && id_field < obj_end) {
            ScenarioDialogue d;
            std::string s_id;
            if (extract_json_field(obj_start, obj_end, "\"id\"", s_id)) {
                d.id = atoi(s_id.c_str());
            }
            extract_json_field(obj_start, obj_end, "\"content\"", d.content);
            extract_json_field(obj_start, obj_end, "\"place\"", d.place);
            extract_json_field(obj_start, obj_end, "\"teller\"", d.teller);
            extract_json_field(obj_start, obj_end, "\"title\"", d.title);

            out[d.id] = d;
        }
        p = obj_end + 1;
    }
    free(buf);
}

static void patch_scenario_object(Il2CppObject* scn, const char* scen_id) {
    if (!scn || !scen_id || !*scen_id) return;

    std::string path1 = std::string("/data/data/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN/StoryData/") + scen_id + ".json";
    std::string path2 = std::string("/data/user/0/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN/StoryData/") + scen_id + ".json";
    std::string chosen_path;
    if (access(path1.c_str(), R_OK) == 0) {
        chosen_path = path1;
    } else if (access(path2.c_str(), R_OK) == 0) {
        chosen_path = path2;
    } else {
        return;
    }

    std::unordered_map<int, ScenarioDialogue> dialogues;
    parse_scenario_json_file(chosen_path, dialogues);
    if (dialogues.empty()) return;

    Il2CppClass* scn_class = il2cpp_object_get_class_fn(scn);
    if (!scn_class) return;

    void* dl_field = il2cpp_class_get_field_from_name_fn(scn_class, "dataList");
    if (!dl_field) dl_field = il2cpp_class_get_field_from_name_fn(scn_class, "_dataList");
    if (!dl_field) {
        void* iter = nullptr;
        void* f = nullptr;
        while ((f = il2cpp_class_get_fields_fn(scn_class, &iter))) {
            const char* fn = il2cpp_field_get_name_fn(f);
            if (fn && (strstr(fn, "dataList") || strstr(fn, "DataList") || strstr(fn, "scenario"))) {
                dl_field = f;
                break;
            }
        }
    }
    if (!dl_field) return;

    uint32_t dl_off = il2cpp_field_get_offset_fn(dl_field);
    Il2CppObject* dlist = *(Il2CppObject**)((char*)scn + dl_off);
    if (!dlist) return;

    Il2CppObject* ditems = *(Il2CppObject**)((char*)dlist + 16);
    int dsize = *(int*)((char*)dlist + 24);
    if (!ditems || dsize <= 0) return;

    Il2CppObject** arr = (Il2CppObject**)((char*)ditems + sizeof(void*)*4);

    static uint32_t off_id = 16;
    static uint32_t off_content = 0;
    static uint32_t off_place = 0;
    static uint32_t off_teller = 0;
    static uint32_t off_title = 0;
    static bool offsets_resolved = false;

    if (!offsets_resolved && dsize > 0 && arr[0]) {
        Il2CppClass* item_class = il2cpp_object_get_class_fn(arr[0]);
        void* iter = nullptr;
        void* f = nullptr;
        while ((f = il2cpp_class_get_fields_fn(item_class, &iter))) {
            const char* fn = il2cpp_field_get_name_fn(f);
            uint32_t off = il2cpp_field_get_offset_fn(f);
            if (strcmp(fn, "id") == 0) off_id = off;
            else if (strcmp(fn, "content") == 0) off_content = off;
            else if (strcmp(fn, "place") == 0) off_place = off;
            else if (strcmp(fn, "teller") == 0) off_teller = off;
            else if (strcmp(fn, "title") == 0) off_title = off;
        }
        offsets_resolved = true;
        LOGI("[LLC-CORE] ScenarioData item class: %s, offsets: id=%u, content=%u, place=%u, teller=%u, title=%u",
             il2cpp_class_get_name_fn(item_class), off_id, off_content, off_place, off_teller, off_title);
    }

    int patched = 0;
    for (int i = 0; i < dsize; ++i) {
        Il2CppObject* item = arr[i];
        if (!item) continue;
        int id = *(int*)((char*)item + off_id);
        auto it = dialogues.find(id);
        if (it != dialogues.end()) {
            if (off_content && !it->second.content.empty()) {
                *(Il2CppString**)((char*)item + off_content) = il2cpp_string_new_fn(it->second.content.c_str());
            }
            if (off_place && !it->second.place.empty()) {
                *(Il2CppString**)((char*)item + off_place) = il2cpp_string_new_fn(it->second.place.c_str());
            }
            if (off_teller && !it->second.teller.empty()) {
                *(Il2CppString**)((char*)item + off_teller) = il2cpp_string_new_fn(it->second.teller.c_str());
            }
            if (off_title && !it->second.title.empty()) {
                *(Il2CppString**)((char*)item + off_title) = il2cpp_string_new_fn(it->second.title.c_str());
            }
            patched++;
        }
    }
    LOGI("[LLC-CORE] Scenario '%s' patched: %d / %d lines updated with Chinese dialogues!", scen_id, patched, dsize);
}

typedef Il2CppObject* (*GetScenario_t)(void* a0, void* a1, void* a2);
static GetScenario_t orig_GetScenario = nullptr;
static bool g_is_scenario_instance = false;

static Il2CppObject* hook_GetScenario(void* a0, void* a1, void* a2) {
    Il2CppString* id_str = nullptr;
    if (g_is_scenario_instance) {
        id_str = (Il2CppString*)a1;
    } else {
        id_str = (Il2CppString*)a0;
    }

    Il2CppObject* scn = orig_GetScenario(a0, a1, a2);
    if (!scn || !id_str || (uintptr_t)id_str <= 0x10000) return scn;

    char scen_id[128] = {0};
    const uint16_t* u = il2cpp_string_chars_fn(id_str);
    for (int x = 0; x < 127 && u[x]; ++x) scen_id[x] = (char)u[x];

    patch_scenario_object(scn, scen_id);
    return scn;
}

static bool g_story_hook_initialized = false;
static void init_story_hook() {
    if (g_story_hook_initialized) return;
    Il2CppClass* sdp_class = find_class_any("StorySystem", "StoryDataParser");
    if (!sdp_class) {
        sdp_class = find_class_any("", "StoryDataParser");
    }
    if (!sdp_class) {
        LOGE("[LLC-CORE] StoryDataParser class not found!");
        return;
    }

    const MethodInfo* mi = il2cpp_class_get_method_from_name_fn(sdp_class, "GetScenario", 2);
    if (!mi || !mi->methodPointer) {
        LOGE("[LLC-CORE] StoryDataParser.GetScenario method not found!");
        return;
    }

    g_is_scenario_instance = il2cpp_method_is_instance_fn ? il2cpp_method_is_instance_fn(mi) : false;
    LOGI("[LLC-CORE] Found StoryDataParser.GetScenario at %p (is_instance: %d). Installing A64 inline hook...",
         mi->methodPointer, g_is_scenario_instance);

    A64HookFunction(mi->methodPointer, (void*)hook_GetScenario, (void**)&orig_GetScenario);
    g_story_hook_initialized = true;
    LOGI("[LLC-CORE] StoryDataParser.GetScenario inline hook installed successfully! (orig: %p)", orig_GetScenario);
}

static int patch_ui_list(Il2CppObject* ui_list_obj, const char* list_name) {
    if (!ui_list_obj) return 0;
    Il2CppObject* dic = *(Il2CppObject**)((char*)ui_list_obj + 16);
    if (!dic) return 0;

    Il2CppClass* dic_class = il2cpp_object_get_class_fn(dic);
    const MethodInfo* set_item_mi = dic_class ? il2cpp_class_get_method_from_name_fn(dic_class, "set_Item", 2) : nullptr;
    if (!set_item_mi) {
        LOGE("[LLC-CORE] Failed to resolve set_Item on %s._dic", list_name);
        return 0;
    }

    // Step 1: Update existing entries in-place
    Il2CppObject* entries = *(Il2CppObject**)((char*)dic + 24);
    int count = *(int*)((char*)dic + 32);
    int updated_in_place = 0;

    if (entries && count > 0) {
        char* items_base = (char*)entries + sizeof(void*) * 4;
        for (int e = 0; e < count; ++e) {
            char* entry_ptr = items_base + e * 24;
            Il2CppString* k = *(Il2CppString**)(entry_ptr + 8);
            Il2CppObject* v = *(Il2CppObject**)(entry_ptr + 16);
            if (!k || !v) continue;
            char kbuf[128] = {0};
            const uint16_t* u = il2cpp_string_chars_fn(k);
            for (int x = 0; x < 127 && u[x]; ++x) kbuf[x] = (char)u[x];

            auto it = g_ui_map.find(kbuf);
            if (it != g_ui_map.end()) {
                *(Il2CppString**)((char*)v + 24) = il2cpp_string_new_fn(it->second.c_str());
                updated_in_place++;
            }
        }
    }

    // Step 2: Determine element class for new insertions
    Il2CppClass* luil_class = il2cpp_object_get_class_fn(ui_list_obj);
    const MethodInfo* get_data_mi = luil_class ? il2cpp_class_get_method_from_name_fn(luil_class, "GetData", 1) : nullptr;
    Il2CppClass* elem_class = nullptr;
    if (get_data_mi) {
        Il2CppString* dummy_k = il2cpp_string_new_fn("__dummy_lookup__");
        void* args[1] = { dummy_k };
        Il2CppException* exc = nullptr;
        Il2CppObject* dummy_item = il2cpp_runtime_invoke_fn(get_data_mi, ui_list_obj, args, &exc);
        if (dummy_item) elem_class = il2cpp_object_get_class_fn(dummy_item);
    }

    // Step 3: Insert all remaining UI keys into dictionary
    int inserted = 0;
    if (elem_class) {
        Il2CppException* exc = nullptr;
        for (const auto& kv : g_ui_map) {
            Il2CppObject* elem = il2cpp_object_new_fn(elem_class);
            Il2CppString* k_str = il2cpp_string_new_fn(kv.first.c_str());
            Il2CppString* c_str = il2cpp_string_new_fn(kv.second.c_str());
            *(Il2CppString**)((char*)elem + 16) = k_str;
            *(Il2CppString**)((char*)elem + 24) = c_str;
            void* si_args[2] = { k_str, elem };
            il2cpp_runtime_invoke_fn(set_item_mi, dic, si_args, &exc);
            if (!exc) inserted++;
        }
    }

    LOGI("[LLC-CORE] %s synchronized: %d in-place updated, %d total keys inserted in _dic.",
         list_name, updated_in_place, inserted);
    return updated_in_place + inserted;
}

static Il2CppObject* g_live_text_dataset = nullptr;

typedef Il2CppObject* (*get_UIList_t)(Il2CppObject* this_ptr);
static get_UIList_t orig_get_UIList = nullptr;

static Il2CppObject* hook_get_UIList(Il2CppObject* this_ptr) {
    if (!g_live_text_dataset && this_ptr) {
        g_live_text_dataset = this_ptr;
        LOGI("[LLC-CORE] [HOOK] Captured live TextDataSet via get_UIList: %p", this_ptr);
    }
    return orig_get_UIList(this_ptr);
}

typedef void (*LoadRemote_t)(Il2CppObject* this_ptr, int lang);
static LoadRemote_t orig_LoadRemote = nullptr;

static void hook_LoadRemote(Il2CppObject* this_ptr, int lang) {
    LOGI("[LLC-CORE] [HOOK] TextDataSet.LoadRemote called! this=%p, lang=%d", this_ptr, lang);
    g_live_text_dataset = this_ptr;
    orig_LoadRemote(this_ptr, lang);
    LOGI("[LLC-CORE] [HOOK] TextDataSet.LoadRemote completed! Live TextDataSet captured.");
}

static bool g_tds_hooks_installed = false;
static void init_tds_hooks() {
    if (g_tds_hooks_installed) return;
    Il2CppClass* tds_class = find_class_any("", "TextDataSet");
    if (!tds_class) return;

    const MethodInfo* gui_mi = il2cpp_class_get_method_from_name_fn(tds_class, "get_UIList", 0);
    if (gui_mi && gui_mi->methodPointer) {
        A64HookFunction(gui_mi->methodPointer, (void*)hook_get_UIList, (void**)&orig_get_UIList);
        LOGI("[LLC-CORE] TextDataSet.get_UIList hook installed at %p", gui_mi->methodPointer);
    }

    const MethodInfo* lr_mi = il2cpp_class_get_method_from_name_fn(tds_class, "LoadRemote", 1);
    if (lr_mi && lr_mi->methodPointer) {
        A64HookFunction(lr_mi->methodPointer, (void*)hook_LoadRemote, (void**)&orig_LoadRemote);
        LOGI("[LLC-CORE] TextDataSet.LoadRemote hook installed at %p", lr_mi->methodPointer);
    }
    g_tds_hooks_installed = true;
}

static Il2CppObject* get_live_text_dataset() {
    init_tds_hooks();
    return g_live_text_dataset;
}

static std::unordered_set<Il2CppObject*> g_patched_fonts;

static void inject_fallback_font_into(Il2CppObject* font_asset) {
    if (!font_asset || !g_shs_font || font_asset == g_shs_font) return;
    if (g_patched_fonts.find(font_asset) != g_patched_fonts.end()) return;

    Il2CppClass* f_class = il2cpp_object_get_class_fn(font_asset);
    const MethodInfo* get_tbl_mi = find_method_in_hierarchy(f_class, "get_fallbackFontAssetTable", 0);
    if (!get_tbl_mi) {
        get_tbl_mi = find_method_in_hierarchy(f_class, "get_fallbackFontAssets", 0);
    }
    if (!get_tbl_mi) return;

    Il2CppException* exc = nullptr;
    Il2CppObject* tbl = il2cpp_runtime_invoke_fn(get_tbl_mi, font_asset, nullptr, &exc);
    if (!tbl || exc) return;

    Il2CppClass* l_class = il2cpp_object_get_class_fn(tbl);
    const MethodInfo* contains_mi = find_method_in_hierarchy(l_class, "Contains", 1);
    if (contains_mi) {
        void* chk_args[1] = { g_shs_font };
        Il2CppObject* res = il2cpp_runtime_invoke_fn(contains_mi, tbl, chk_args, &exc);
        if (res && !exc && *(bool*)il2cpp_object_unbox_fn(res)) {
            g_patched_fonts.insert(font_asset);
            return;
        }
    }

    const MethodInfo* add_mi = find_method_in_hierarchy(l_class, "Add", 1);
    if (add_mi) {
        void* add_args[1] = { g_shs_font };
        il2cpp_runtime_invoke_fn(add_mi, tbl, add_args, &exc);
        if (!exc) {
            g_patched_fonts.insert(font_asset);
            Il2CppClass* unity_obj_class = find_class_any("UnityEngine", "Object");
            const MethodInfo* get_name_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "get_name", 0) : nullptr;
            Il2CppObject* name_obj = get_name_mi ? il2cpp_runtime_invoke_fn(get_name_mi, font_asset, nullptr, &exc) : nullptr;
            char buf[128] = {0};
            if (name_obj) {
                const uint16_t* u16 = il2cpp_string_chars_fn((Il2CppString*)name_obj);
                for (int k = 0; k < 127 && u16[k]; ++k) buf[k] = (char)u16[k];
            }
            LOGI("[LLC-CORE] Injected Chinese fallback into font: '%s' (%p)", buf, font_asset);
        }
    }
}

static Il2CppObject* find_game_font_shader() {
    Il2CppClass* resources_class = find_class_any("UnityEngine", "Resources");
    Il2CppClass* font_asset_class = find_class_any("TMPro", "TMP_FontAsset");
    Il2CppClass* material_class = find_class_any("UnityEngine", "Material");
    if (!resources_class || !font_asset_class || !material_class) return nullptr;

    const MethodInfo* find_objs_mi = il2cpp_class_get_method_from_name_fn(resources_class, "FindObjectsOfTypeAll", 1);
    const MethodInfo* get_mat_mi = find_method_in_hierarchy(font_asset_class, "get_material", 0);
    const MethodInfo* get_shader_mi = find_method_in_hierarchy(material_class, "get_shader", 0);
    if (!find_objs_mi || !get_mat_mi || !get_shader_mi) return nullptr;

    Il2CppException* exc = nullptr;
    const Il2CppType* font_type = il2cpp_class_get_type_fn(font_asset_class);
    Il2CppObject* font_type_obj = il2cpp_type_get_object_fn(font_type);
    void* f_args[1] = { font_type_obj };
    Il2CppObject* font_arr_obj = il2cpp_runtime_invoke_fn(find_objs_mi, nullptr, f_args, &exc);
    if (!font_arr_obj || exc) return nullptr;

    uintptr_t total_fonts = il2cpp_array_length_fn(font_arr_obj);
    Il2CppObject** font_items = (Il2CppObject**)((char*)font_arr_obj + sizeof(void*)*4);
    for (uintptr_t i = 0; i < total_fonts; ++i) {
        if (!font_items[i] || font_items[i] == g_shs_font || font_items[i] == g_empty_font) continue;
        Il2CppObject* mat = il2cpp_runtime_invoke_fn(get_mat_mi, font_items[i], nullptr, &exc);
        if (mat && !exc) {
            Il2CppObject* shader = il2cpp_runtime_invoke_fn(get_shader_mi, mat, nullptr, &exc);
            if (shader && !exc) {
                Il2CppClass* unity_obj_class = find_class_any("UnityEngine", "Object");
                const MethodInfo* get_name_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "get_name", 0) : nullptr;
                Il2CppObject* sname = get_name_mi ? il2cpp_runtime_invoke_fn(get_name_mi, shader, nullptr, &exc) : nullptr;
                char buf[128] = {0};
                if (sname) {
                    const uint16_t* u16 = il2cpp_string_chars_fn((Il2CppString*)sname);
                    for (int k = 0; k < 127 && u16[k]; ++k) buf[k] = (char)u16[k];
                }
                LOGI("[LLC-CORE] Found valid game font shader: '%s' (%p)", buf, shader);
                return shader;
            }
        }
    }
    return nullptr;
}

static bool ensure_font_material_valid() {
    if (!g_shs_font) return false;
    Il2CppClass* font_asset_class = find_class_any("TMPro", "TMP_FontAsset");
    Il2CppClass* material_class = find_class_any("UnityEngine", "Material");
    if (!font_asset_class || !material_class) return false;

    const MethodInfo* font_get_mat_mi = find_method_in_hierarchy(font_asset_class, "get_material", 0);
    const MethodInfo* font_set_mat_mi = find_method_in_hierarchy(font_asset_class, "set_material", 1);
    const MethodInfo* mat_set_shader_mi = find_method_in_hierarchy(material_class, "set_shader", 1);
    const MethodInfo* mat_set_main_tex_mi = find_method_in_hierarchy(material_class, "set_mainTexture", 1);

    Il2CppException* exc = nullptr;
    Il2CppObject* cur_mat = font_get_mat_mi ? il2cpp_runtime_invoke_fn(font_get_mat_mi, g_shs_font, nullptr, &exc) : nullptr;

    if (!cur_mat && g_shs_material) {
        if (font_set_mat_mi) {
            void* m_args[1] = { g_shs_material };
            il2cpp_runtime_invoke_fn(font_set_mat_mi, g_shs_font, m_args, &exc);
            if (!exc) cur_mat = g_shs_material;
        }
        void* mat_field = il2cpp_class_get_field_from_name_fn(font_asset_class, "m_material");
        if (!mat_field) mat_field = il2cpp_class_get_field_from_name_fn(font_asset_class, "material");
        if (mat_field && cur_mat) {
            il2cpp_field_set_value_fn(g_shs_font, mat_field, cur_mat);
        }
    }

    if (!cur_mat) return false;

    Il2CppObject* game_shader = find_game_font_shader();
    if (!game_shader) return false;

    if (mat_set_shader_mi) {
        void* s_args[1] = { game_shader };
        il2cpp_runtime_invoke_fn(mat_set_shader_mi, cur_mat, s_args, &exc);
        LOGI("[LLC-CORE] Successfully bound Android Vulkan TMP shader (%p) to Chinese font material!", game_shader);
    }

    if (g_empty_material && mat_set_shader_mi) {
        void* s_args[1] = { game_shader };
        il2cpp_runtime_invoke_fn(mat_set_shader_mi, g_empty_material, s_args, &exc);
    }

    if (g_shs_atlas && mat_set_main_tex_mi) {
        void* t_args[1] = { g_shs_atlas };
        il2cpp_runtime_invoke_fn(mat_set_main_tex_mi, cur_mat, t_args, &exc);
        LOGI("[LLC-CORE] Injected atlas texture into g_shs_font material.");
    }

    return true;
}

static void inject_fallback_to_all_loaded_fonts() {
    Il2CppClass* resources_class = find_class_any("UnityEngine", "Resources");
    Il2CppClass* font_asset_class = find_class_any("TMPro", "TMP_FontAsset");
    if (!resources_class || !font_asset_class || !g_shs_font) return;

    const MethodInfo* find_objs_mi = il2cpp_class_get_method_from_name_fn(resources_class, "FindObjectsOfTypeAll", 1);
    if (!find_objs_mi) return;

    Il2CppException* exc = nullptr;
    const Il2CppType* font_type = il2cpp_class_get_type_fn(font_asset_class);
    Il2CppObject* font_type_obj = il2cpp_type_get_object_fn(font_type);
    void* f_args[1] = { font_type_obj };
    Il2CppObject* font_arr_obj = il2cpp_runtime_invoke_fn(find_objs_mi, nullptr, f_args, &exc);
    if (!font_arr_obj || exc) return;

    uintptr_t total_fonts = il2cpp_array_length_fn(font_arr_obj);
    Il2CppObject** font_items = (Il2CppObject**)((char*)font_arr_obj + sizeof(void*)*4);
    for (uintptr_t i = 0; i < total_fonts; ++i) {
        if (font_items[i]) {
            inject_fallback_font_into(font_items[i]);
        }
    }
}

static void refresh_title_screen_loaders() {
    Il2CppClass* resources_class = find_class_any("UnityEngine", "Resources");
    Il2CppClass* utdl_class = find_class_any("", "UITextDataLoader");
    if (!resources_class || !utdl_class) return;

    const MethodInfo* find_objs_mi = il2cpp_class_get_method_from_name_fn(resources_class, "FindObjectsOfTypeAll", 1);
    if (!find_objs_mi) return;

    const MethodInfo* utdl_set_text_mi = il2cpp_class_get_method_from_name_fn(utdl_class, "SetText", 0);
    if (!utdl_set_text_mi) return;

    Il2CppException* exc = nullptr;
    const Il2CppType* utdl_type = il2cpp_class_get_type_fn(utdl_class);
    Il2CppObject* utdl_type_obj = il2cpp_type_get_object_fn(utdl_type);
    void* u_args[1] = { utdl_type_obj };
    Il2CppObject* arr_obj = il2cpp_runtime_invoke_fn(find_objs_mi, nullptr, u_args, &exc);
    if (!arr_obj || exc) return;

    uintptr_t total = il2cpp_array_length_fn(arr_obj);
    Il2CppObject** items = (Il2CppObject**)((char*)arr_obj + sizeof(void*)*4);
    int refreshed = 0;

    for (uintptr_t i = 0; i < total; ++i) {
        if (!items[i]) continue;
        il2cpp_runtime_invoke_fn(utdl_set_text_mi, items[i], nullptr, &exc);
        if (!exc) refreshed++;
    }
    LOGI("[LLC-CORE] Refreshed %d/%lu title screen UITextDataLoaders.", refreshed, (unsigned long)total);
}

static void* pure_native_engine_thread(void*) {
    LOGI("[LLC-CORE] Pure Native Engine Thread active. Loading local UI dictionaries...");
    load_all_ui_jsons("/data/user/0/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN");
    load_all_content_jsons("/data/user/0/com.ProjectMoon.LimbusCompany/files/Lang/LLC_zh-CN");

    LOGI("[LLC-CORE] Awaiting Unity engine initialization (sleep 4s)...");
    sleep(4);

    LOGI("[LLC-CORE] Polling for Il2CppDomain...");
    Il2CppDomain* domain = nullptr;
    for (int retry = 0; retry < 60; ++retry) {
        domain = il2cpp_domain_get_fn();
        if (domain) break;
        usleep(250000); // 250ms
    }

    if (!domain) {
        LOGE("[LLC-CORE] Timeout waiting for Il2CppDomain!");
        return nullptr;
    }

    il2cpp_thread_attach_fn(domain);
    LOGI("[LLC-CORE] Successfully attached to Il2CppDomain: %p", domain);

    // Wait for core classes
    Il2CppClass* ab_class = nullptr;
    Il2CppClass* async_op_class = nullptr;
    Il2CppClass* font_asset_class = nullptr;

    for (int retry = 0; retry < 100; ++retry) {
        ab_class = find_class_any("UnityEngine", "AssetBundle");
        async_op_class = find_class_any("UnityEngine", "AsyncOperation");
        font_asset_class = find_class_any("TMPro", "TMP_FontAsset");
        if (ab_class && async_op_class && font_asset_class) break;
        usleep(100000);
    }

    if (!ab_class || !async_op_class) {
        LOGE("[LLC-CORE] Failed to resolve required classes!");
        return nullptr;
    }

    // Resolve AssetBundle async loader methods
    void* ab_iter = nullptr;
    const MethodInfo* mi = nullptr;
    const MethodInfo* load_async_mi = nullptr;
    const MethodInfo* load_all_async_type_mi = nullptr;

    while ((mi = il2cpp_class_get_methods_fn(ab_class, &ab_iter))) {
        const char* mname = il2cpp_method_get_name_fn(mi);
        if (!mname) continue;
        uint32_t pcount = il2cpp_method_get_param_count_fn(mi);
        if (strcmp(mname, "LoadFromFileAsync") == 0 && pcount == 1) {
            load_async_mi = mi;
        } else if (strcmp(mname, "LoadAllAssetsAsync") == 0 && pcount == 1) {
            load_all_async_type_mi = mi;
        }
    }
    LOGI("[LLC-CORE] Resolved methods: LoadFromFileAsync=%p, LoadAllAssetsAsync(Type)=%p",
         load_async_mi, load_all_async_type_mi);

    const MethodInfo* is_done_mi = il2cpp_class_get_method_from_name_fn(async_op_class, "get_isDone", 0);

    if (load_async_mi && is_done_mi && load_all_async_type_mi) {
        const char* font_bundle_path = "/data/user/0/com.ProjectMoon.LimbusCompany/files/tmpchinesefont";
        LOGI("[LLC-CORE] Loading Chinese Font AssetBundle from: %s", font_bundle_path);

        Il2CppString* font_path_str = il2cpp_string_new_fn(font_bundle_path);
        void* args[1] = { font_path_str };
        Il2CppException* exc = nullptr;
        Il2CppObject* req_obj = il2cpp_runtime_invoke_fn(load_async_mi, nullptr, args, &exc);

        if (req_obj) {
            for (int retry = 0; retry < 150; ++retry) {
                Il2CppObject* res = il2cpp_runtime_invoke_fn(is_done_mi, req_obj, nullptr, &exc);
                if (res && !exc && *(bool*)il2cpp_object_unbox_fn(res)) break;
                usleep(50000);
            }

            Il2CppClass* req_class = il2cpp_object_get_class_fn(req_obj);
            const MethodInfo* get_ab_mi = req_class ? find_method_in_hierarchy(req_class, "get_assetBundle", 0) : nullptr;
            Il2CppObject* ab_obj = get_ab_mi ? il2cpp_runtime_invoke_fn(get_ab_mi, req_obj, nullptr, &exc) : nullptr;

            if (ab_obj) {
                LOGI("[LLC-CORE] AssetBundle loaded: %p", ab_obj);
                Il2CppObject* assets_array_obj = nullptr;

                Il2CppClass* unity_obj_class = find_class_any("UnityEngine", "Object");
                const Il2CppType* obj_type = unity_obj_class ? il2cpp_class_get_type_fn(unity_obj_class) : nullptr;
                Il2CppObject* obj_type_obj = obj_type ? il2cpp_type_get_object_fn(obj_type) : nullptr;

                if (obj_type_obj) {
                    void* abr_args[1] = { obj_type_obj };
                    Il2CppObject* abr_obj = il2cpp_runtime_invoke_fn(load_all_async_type_mi, ab_obj, abr_args, &exc);
                    if (exc) {
                        log_exception("LoadAllAssetsAsync(Type)", exc);
                    }
                    if (abr_obj && !exc) {
                        for (int retry = 0; retry < 150; ++retry) {
                            Il2CppObject* res = il2cpp_runtime_invoke_fn(is_done_mi, abr_obj, nullptr, &exc);
                            if (res && !exc && *(bool*)il2cpp_object_unbox_fn(res)) break;
                            usleep(50000);
                        }
                        Il2CppClass* abr_class = il2cpp_object_get_class_fn(abr_obj);
                        const MethodInfo* get_all_assets_mi = abr_class ? find_method_in_hierarchy(abr_class, "get_allAssets", 0) : nullptr;
                        if (get_all_assets_mi) {
                            assets_array_obj = il2cpp_runtime_invoke_fn(get_all_assets_mi, abr_obj, nullptr, &exc);
                            if (exc) {
                                log_exception("get_allAssets", exc);
                            }
                        }
                    }
                }

                if (assets_array_obj) {
                    uintptr_t asset_count = il2cpp_array_length_fn(assets_array_obj);
                    const MethodInfo* dont_destroy_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "DontDestroyOnLoad", 1) : nullptr;
                    const MethodInfo* get_name_mi = unity_obj_class ? find_method_in_hierarchy(unity_obj_class, "get_name", 0) : nullptr;

                    LOGI("[LLC-CORE] Total assets loaded from font bundle: %lu", (unsigned long)asset_count);
                    Il2CppObject** items = (Il2CppObject**)((char*)assets_array_obj + sizeof(void*)*4);
                    for (uintptr_t i = 0; i < asset_count; ++i) {
                        Il2CppObject* asset = items[i];
                        if (!asset) continue;
                        Il2CppClass* klass = il2cpp_object_get_class_fn(asset);

                        if (dont_destroy_mi) {
                            void* dd_args[1] = { asset };
                            il2cpp_runtime_invoke_fn(dont_destroy_mi, nullptr, dd_args, &exc);
                        }

                        Il2CppObject* name_obj = get_name_mi ? il2cpp_runtime_invoke_fn(get_name_mi, asset, nullptr, &exc) : nullptr;
                        char buf[128] = {0};
                        if (name_obj) {
                            const uint16_t* u16 = il2cpp_string_chars_fn((Il2CppString*)name_obj);
                            for (int k = 0; k < 127 && u16[k]; ++k) buf[k] = (char)u16[k];
                        }

                        LOGI("[LLC-CORE] Bundle asset [%lu]: %s (class: %s)", (unsigned long)i, buf, il2cpp_class_get_name_fn(klass));

                        const char* kname = klass ? il2cpp_class_get_name_fn(klass) : "";
                        if (strcmp(kname, "TMP_FontAsset") == 0) {
                            if (strstr(buf, "SourceHanSans")) {
                                g_shs_font = asset;
                                LOGI("[LLC-CORE] Selected Chinese Font: %s (%p)", buf, g_shs_font);
                            } else if (strstr(buf, "empty")) {
                                g_empty_font = asset;
                                LOGI("[LLC-CORE] Selected Empty Font: %s (%p)", buf, g_empty_font);
                            }
                        } else if (strcmp(kname, "Material") == 0) {
                            if (strstr(buf, "SourceHanSans")) {
                                g_shs_material = asset;
                                LOGI("[LLC-CORE] Selected Chinese Material: %s (%p)", buf, g_shs_material);
                            } else if (strstr(buf, "empty")) {
                                g_empty_material = asset;
                                LOGI("[LLC-CORE] Selected Empty Material: %s (%p)", buf, g_empty_material);
                            }
                        } else if (strcmp(kname, "Texture2D") == 0) {
                            if (strstr(buf, "SourceHanSans") && !g_shs_atlas) {
                                g_shs_atlas = asset;
                                LOGI("[LLC-CORE] Selected Chinese Atlas Texture: %s (%p)", buf, g_shs_atlas);
                            }
                        }
                    }
                }
            }
        }
    }

    // Ensure Chinese font material and shader are valid before injection
    ensure_font_material_valid();

    // Font injection: pure memory fallback table injection (NO TMP_Settings)
    if (g_shs_font) {
        LOGI("[LLC-CORE] Injecting Chinese Font fallback into all loaded fonts in memory...");
        inject_fallback_to_all_loaded_fonts();
        LOGI("[LLC-CORE] Initial font injection complete.");
    }

    // Fast Polling Watcher: locate TextDataSet and keep dictionaries synchronized
    LOGI("[LLC-CORE] Entering TextDataSet synchronization watcher (continuous runtime loop)...");
    bool login_patched = false;
    bool ui_list_patched = false;
    bool remote_patched = false;
    bool shader_bound = false;
    bool logged_fields = false;
    uint64_t loop = 0;

    while (true) {
        loop++;

        if (!shader_bound) {
            if (ensure_font_material_valid()) {
                shader_bound = true;
                LOGI("[LLC-CORE] Font material and Android Vulkan shader successfully configured.");
                inject_fallback_to_all_loaded_fonts();
                if (login_patched) {
                    refresh_title_screen_loaders();
                }
            }
        }

        Il2CppObject* live_tds = get_live_text_dataset();

        if (live_tds) {
            Il2CppObject* login_ui_list = *(Il2CppObject**)((char*)live_tds + 616);
            Il2CppObject* ui_list = *(Il2CppObject**)((char*)live_tds + 16);
            bool is_remote = *(bool*)((char*)live_tds + 656);

            init_story_hook();

            if (!login_patched && login_ui_list) {
                int p = patch_ui_list(login_ui_list, "_loginUIList");
                if (p > 0) {
                    login_patched = true;
                    LOGI("[LLC-CORE] _loginUIList successfully patched with %d entries!", p);
                    if (shader_bound) {
                        refresh_title_screen_loaders();
                    }
                }
            }

            if (!ui_list_patched && ui_list) {
                int p = patch_ui_list(ui_list, "_uiList");
                if (p > 0) {
                    ui_list_patched = true;
                    LOGI("[LLC-CORE] _uiList initial synchronized with %d entries!", p);
                }
            }

            if (!remote_patched && ui_list && is_remote) {
                int p = patch_ui_list(ui_list, "_uiList (remote)");
                if (p > 0) {
                    remote_patched = true;
                    LOGI("[LLC-CORE] Remote localization loaded! _uiList synchronized with %d entries.", p);
                }
            }

            // Periodically check for any new font assets every ~2.5s (10 ticks)
            if (loop % 10 == 0 && shader_bound) {
                inject_fallback_to_all_loaded_fonts();
            }

            if (loop % 20 == 0) {
                Il2CppObject* pers_list = *(Il2CppObject**)((char*)live_tds + 64);
                Il2CppObject* skill_list = *(Il2CppObject**)((char*)live_tds + 88);
                Il2CppObject* passive_list = *(Il2CppObject**)((char*)live_tds + 96);
                Il2CppObject* buf_list = *(Il2CppObject**)((char*)live_tds + 104);
                Il2CppObject* ego_list = *(Il2CppObject**)((char*)live_tds + 80);
                Il2CppObject* keyword_list = *(Il2CppObject**)((char*)live_tds + 48);
                Il2CppObject* char_list = *(Il2CppObject**)((char*)live_tds + 40);

                Il2CppObject* pv_list = *(Il2CppObject**)((char*)live_tds + 280);
                Il2CppObject* av_list = *(Il2CppObject**)((char*)live_tds + 288);
                Il2CppObject* ev_list = *(Il2CppObject**)((char*)live_tds + 296);
                Il2CppObject* bgm_list = *(Il2CppObject**)((char*)live_tds + 304);

                Il2CppObject* theater_list = *(Il2CppObject**)((char*)live_tds + 264);
                Il2CppObject* banner_list = *(Il2CppObject**)((char*)live_tds + 416);

                Il2CppObject* chapter_list = *(Il2CppObject**)((char*)live_tds + 192);
                Il2CppObject* node_list = *(Il2CppObject**)((char*)live_tds + 208);
                Il2CppObject* story_dungeon_node = *(Il2CppObject**)((char*)live_tds + 224);
                Il2CppObject* railway_dungeon_node = *(Il2CppObject**)((char*)live_tds + 232);

                auto get_cnt = [](Il2CppObject* jdl) -> int {
                    if (!jdl) return -1;
                    Il2CppObject* d = *(Il2CppObject**)((char*)jdl + 16);
                    if (!d) return -2;
                    return *(int*)((char*)d + 32);
                };

                int pc = get_cnt(pers_list);
                int sc = get_cnt(skill_list);
                int pasc = get_cnt(passive_list);
                int bc = get_cnt(buf_list);
                int ec = get_cnt(ego_list);
                int kc = get_cnt(keyword_list);
                int cc = get_cnt(char_list);
                int pvc = get_cnt(pv_list);
                int tc = get_cnt(theater_list);
                int bnc = get_cnt(banner_list);
                int chc = get_cnt(chapter_list);
                int nc = get_cnt(node_list);

                LOGI("[LLC-CORE] [WATCHER #%llu] pers=%d, skill=%d, pass=%d, buf=%d, ego=%d, kw=%d, char=%d, pv=%d, theater=%d, banner=%d, chap=%d, node=%d (remote=%d)",
                     (unsigned long long)loop, pc, sc, pasc, bc, ec, kc, cc, pvc, tc, bnc, chc, nc, is_remote);

                if (pc > 0) {
                    static int last_pers_patched_count = 0;
                    if (pc != last_pers_patched_count) {
                        last_pers_patched_count = pc;
                        patch_personality_list(pers_list);
                    }
                }

                if (pasc > 0) {
                    static int last_pass_patched_count = 0;
                    if (pasc != last_pass_patched_count) {
                        last_pass_patched_count = pasc;
                        patch_passive_list(passive_list);
                    }
                }

                if (bc > 0) {
                    static int last_buf_patched_count = 0;
                    if (bc != last_buf_patched_count) {
                        last_buf_patched_count = bc;
                        patch_buf_list(buf_list, "_bufList");
                    }
                }

                if (kc > 0) {
                    static int last_kw_patched_count = 0;
                    if (kc != last_kw_patched_count) {
                        last_kw_patched_count = kc;
                        patch_buf_list(keyword_list, "_keywordList");
                    }
                }

                if (ec > 0) {
                    static int last_ego_patched_count = 0;
                    if (ec != last_ego_patched_count) {
                        last_ego_patched_count = ec;
                        patch_ego_list(ego_list);
                    }
                }

                if (cc > 0) {
                    static int last_char_patched_count = 0;
                    if (cc != last_char_patched_count) {
                        last_char_patched_count = cc;
                        patch_character_list(char_list);
                    }
                }

                if (sc > 0) {
                    static int last_skill_patched_count = 0;
                    if (sc != last_skill_patched_count) {
                        last_skill_patched_count = sc;
                        patch_skill_list(skill_list);
                    }
                }

                if (pvc > 0) {
                    static int last_pv_count = 0;
                    if (pvc != last_pv_count) {
                        last_pv_count = pvc;
                        patch_voice_list(pv_list, "_personalityVoiceText");
                        patch_voice_list(av_list, "_announcerVoiceText");
                        patch_voice_list(ev_list, "_egoVoiceText");
                        patch_voice_list(bgm_list, "_bgmLyricsText");
                    }
                }

                if (tc > 0) {
                    static int last_tc = 0;
                    if (tc != last_tc) {
                        last_tc = tc;
                        patch_theater_list(theater_list);
                    }
                }

                if (bnc > 0) {
                    static int last_bnc = 0;
                    if (bnc != last_bnc) {
                        last_bnc = bnc;
                        patch_banner_list(banner_list);
                    }
                }

                if (chc > 0) {
                    static int last_chc = 0;
                    if (chc != last_chc) {
                        last_chc = chc;
                        patch_stage_chapter_list(chapter_list);
                    }
                }

                if (nc > 0) {
                    static int last_nc = 0;
                    if (nc != last_nc) {
                        last_nc = nc;
                        patch_stage_node_list(node_list, "_stageNodeText");
                        patch_stage_node_list(story_dungeon_node, "_storyDungeonNodeText");
                        patch_stage_node_list(railway_dungeon_node, "_railwayDungeonNodeText");
                    }
                }
            }
        }

        usleep(250000); // 250ms
    }

    LOGI("[LLC-CORE] Watcher loop complete.");
    return nullptr;
}

extern "C" __attribute__((visibility("default"))) void llc_core_init() {
    LOGI("[LLC-CORE] Stage 2 Core Initializing (100%% Data-Only Architecture)...");
    if (!init_il2cpp_api()) return;

    pthread_t tid;
    if (pthread_create(&tid, nullptr, pure_native_engine_thread, nullptr) == 0) {
        pthread_detach(tid);
    }
}