#pragma once
#include <dirent.h>
#include <vector>
#include <algorithm>

// Root-private temporary-file cleanup. Deliberately operates only below Store::directory.
namespace manager {
namespace private_cleanup_detail {
constexpr uint64_t max_bytes = 512ull * 1024 * 1024;
constexpr size_t max_entries = 30000;
constexpr int max_depth = 16;
struct Entry { std::string path; uint64_t bytes = 0; std::string fingerprint; };
struct Scan { std::vector<Entry> items; size_t entries = 0; uint64_t bytes = 0; std::string fingerprint; };
inline std::string hex64(const std::string& s) { return resource_pack::digest(s); }
inline std::string stat_key(const std::string& path, const struct stat& st) {
    return std::to_string(path.size()) + ":" + path + ":" + std::to_string(st.st_dev) + ":" + std::to_string(st.st_ino) + ":" +
        std::to_string(st.st_mode) + ":" + std::to_string(st.st_uid) + ":" + std::to_string(st.st_size) + ":" +
        std::to_string(st.st_mtim.tv_sec) + ":" + std::to_string(st.st_mtim.tv_nsec) + ":" +
        std::to_string(st.st_ctim.tv_sec) + ":" + std::to_string(st.st_ctim.tv_nsec) + ";";
}
inline bool digits(const std::string& x) { return !x.empty() && std::all_of(x.begin(), x.end(), [](unsigned char c){ return c >= '0' && c <= '9'; }); }
inline bool candidate_name(const std::string& n) {
    if (n.rfind("candidate-", 0) != 0) return false;
    auto rest = n.substr(10); auto dash = rest.find('-');
    return dash != std::string::npos && digits(rest.substr(0, dash)) && digits(rest.substr(dash + 1));
}
inline bool asset_name(const std::string& n) {
    if (n.rfind("asset-", 0) != 0) return false;
    auto dash = n.find('-', 6), suffix = n.find(".zip", dash == n.npos ? 0 : dash + 1);
    if (dash == n.npos || !digits(n.substr(6, dash - 6))) return false;
    auto digest = n.substr(dash + 1, 64);
    if (!resource_pack::digest_valid(digest) || suffix != dash + 65) return false;
    auto tail = n.substr(suffix);
    if (tail == ".zip") return true;
    if (tail.rfind(".zip.part-", 0) != 0) return false;
    auto rest = tail.substr(10); auto sep = rest.find('-');
    return sep != rest.npos && digits(rest.substr(0, sep)) && digits(rest.substr(sep + 1));
}
inline bool import_name(const std::string& n) {
    constexpr const char* p = "import-manifest-";
    if (n.rfind(p, 0) != 0 || n.size() < 22 || n.substr(n.size()-5) != ".json") return false;
    auto body = n.substr(16, n.size()-21); auto dash = body.find('-');
    return dash != body.npos && digits(body.substr(0, dash)) && digits(body.substr(dash+1));
}
inline void bump(Scan& s, uint64_t size) {
    require(++s.entries <= max_entries, "cleanup_scan_limit_entries");
    require(size <= max_bytes && s.bytes <= max_bytes - size, "cleanup_scan_limit_bytes");
    s.bytes += size;
}
inline std::vector<std::string> names(int fd) {
    int dupfd = openat(fd, ".", O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW); require(dupfd >= 0, "cleanup_scan_failed"); DIR* d = fdopendir(dupfd);
    if (!d) { close(dupfd); throw std::runtime_error("cleanup_scan_failed"); }
    std::vector<std::string> out; errno = 0;
    while (auto* ent = readdir(d)) { std::string n(ent->d_name); if (n != "." && n != "..") { require(out.size() < max_entries, "cleanup_scan_limit_entries"); out.push_back(n); } }
    int err = errno; closedir(d); require(!err, "cleanup_scan_failed"); std::sort(out.begin(), out.end()); return out;
}
inline void tree(int fd, const std::string& rel, int depth, Scan& s, bool& safe, std::string& fp, uint64_t& bytes) {
    if (depth > max_depth) { safe = false; return; }
    for (const auto& n : names(fd)) {
        struct stat st{}; if (fstatat(fd, n.c_str(), &st, AT_SYMLINK_NOFOLLOW)) { safe = false; continue; }
        auto p = rel + "/" + n; bump(s, S_ISREG(st.st_mode) && st.st_size > 0 ? static_cast<uint64_t>(st.st_size) : 0); fp += stat_key(p, st);
        if (st.st_uid != geteuid() || (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))) { safe = false; continue; }
        if (S_ISREG(st.st_mode)) { if (st.st_size < 0 || static_cast<uint64_t>(st.st_size) > max_bytes - bytes) { safe = false; continue; } bytes += st.st_size; continue; }
        int child = openat(fd, n.c_str(), O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if (child < 0) { safe = false; continue; }
        Fd c(child); struct stat now{}; if (fstat(child, &now) || now.st_dev != st.st_dev || now.st_ino != st.st_ino) { safe = false; continue; }
        tree(child, p, depth + 1, s, safe, fp, bytes);
    }
}
inline bool verify_delete(int parent, const std::string& name, const struct stat& expected);
inline bool remove_tree(int parent, const std::string& name, const struct stat& expected) {
    struct stat now{}; if (fstatat(parent, name.c_str(), &now, AT_SYMLINK_NOFOLLOW) || now.st_dev != expected.st_dev || now.st_ino != expected.st_ino || now.st_mode != expected.st_mode) return false;
    if (now.st_uid != geteuid()) return false;
    if (S_ISREG(now.st_mode)) return unlinkat(parent, name.c_str(), 0) == 0 && fsync(parent) == 0;
    if (!S_ISDIR(now.st_mode)) return false;
    Fd d(openat(parent, name.c_str(), O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    struct stat opened{}; if (fstat(d.value, &opened) || opened.st_dev != now.st_dev || opened.st_ino != now.st_ino) return false;
    for (const auto& child : names(d.value)) {
        struct stat st{}; if (fstatat(d.value, child.c_str(), &st, AT_SYMLINK_NOFOLLOW) || st.st_uid != geteuid() || (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) || !remove_tree(d.value, child, st)) return false;
    }
    struct stat still{}; if (fstatat(parent,name.c_str(),&still,AT_SYMLINK_NOFOLLOW) || still.st_dev!=opened.st_dev || still.st_ino!=opened.st_ino || still.st_uid!=geteuid()) return false;
    return unlinkat(parent, name.c_str(), AT_REMOVEDIR) == 0 && fsync(parent) == 0;
}
inline bool verify_delete(int parent, const std::string& name, const struct stat& expected) { return remove_tree(parent, name, expected); }
inline void append_tree_fingerprint(int fd, const std::string& rel, Scan& s, bool& safe, std::string& fp, uint64_t& bytes) {
    tree(fd, rel, 1, s, safe, fp, bytes);
}
inline bool current_fingerprint(int parent, const Entry& e) {
    auto slash=e.path.find('/'); auto name=slash==e.path.npos?e.path:e.path.substr(slash+1);
    struct stat st{}; if (fstatat(parent,name.c_str(),&st,AT_SYMLINK_NOFOLLOW)) return false;
    if (e.fingerprint.rfind(stat_key(e.path,st),0)!=0 || st.st_uid!=geteuid()) return false;
    if (S_ISREG(st.st_mode)) return e.fingerprint==stat_key(e.path,st);
    if (!S_ISDIR(st.st_mode)) return false;
    int raw=openat(parent,name.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW); if(raw<0)return false; Fd d(raw);
    struct stat opened{}; if(fstat(raw,&opened)||opened.st_dev!=st.st_dev||opened.st_ino!=st.st_ino)return false;
    Scan s; bool safe=true; std::string sub; uint64_t bytes=0; tree(raw,e.path,1,s,safe,sub,bytes);
    return safe && e.fingerprint==stat_key(e.path,st)+sub;
}
inline Scan scan(Store& store) {
    require(store.locked, "cleanup_busy");
    auto transfer = read_transfer(store); // Invalid prepared state is a hard failure.
    Json prepared = transfer["prepared"];
    std::string keep_candidate; std::string keep_archive;
    if (!prepared.is_null()) {
        keep_candidate = prepared.at("id").get<std::string>();
        const auto& plan = prepared.at("plan"); release_plan::validate(plan);
        keep_archive = "asset-" + std::to_string(plan.at("asset_id").get<uint64_t>()) + "-" + plan.at("sha256").get<std::string>() + ".zip";
    }
    Scan s; struct stat root{}; require(!fstat(store.directory.value, &root), "cleanup_root_stat_failed");
    std::string fp = stat_key("", root) + prepared.dump();
    for (const auto& top : names(store.directory.value)) {
        struct stat st{}; if (fstatat(store.directory.value, top.c_str(), &st, AT_SYMLINK_NOFOLLOW)) continue;
        bump(s, S_ISREG(st.st_mode) && st.st_size > 0 ? static_cast<uint64_t>(st.st_size) : 0); fp += stat_key(top, st);
        if (top == "candidates" && S_ISDIR(st.st_mode)) {
            Fd d(openat(store.directory.value, top.c_str(), O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
            for (const auto& n : names(d.value)) {
                struct stat item{}; if (fstatat(d.value,n.c_str(),&item,AT_SYMLINK_NOFOLLOW)) continue;
                auto path = top + "/" + n; bump(s,S_ISREG(item.st_mode) && item.st_size > 0 ? static_cast<uint64_t>(item.st_size) : 0); fp += stat_key(path,item);
                if (!candidate_name(n) || n == keep_candidate || !S_ISDIR(item.st_mode) || item.st_uid != geteuid()) continue;
                bool safe = true; std::string sub; uint64_t bytes = 0; auto before=s.entries;
                Fd c(openat(d.value,n.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
                if (c.value < 0) continue;
                struct stat opened{}; if (fstat(c.value,&opened)||opened.st_dev!=item.st_dev||opened.st_ino!=item.st_ino) continue;
                append_tree_fingerprint(c.value,path,s,safe,sub,bytes);
                if (safe) s.items.push_back({path,bytes,stat_key(path,item)+sub});
                (void)before;
            }
        } else if (top == "downloads" && S_ISDIR(st.st_mode)) {
            Fd d(openat(store.directory.value, top.c_str(), O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
            for (const auto& n : names(d.value)) {
                struct stat item{}; if(fstatat(d.value,n.c_str(),&item,AT_SYMLINK_NOFOLLOW)) continue;
                auto path=top+"/"+n; bump(s,S_ISREG(item.st_mode) && item.st_size > 0 ? static_cast<uint64_t>(item.st_size) : 0); fp+=stat_key(path,item);
                if (asset_name(n) && n != keep_archive && S_ISREG(item.st_mode) && item.st_uid==geteuid()) s.items.push_back({path,static_cast<uint64_t>(std::max<off_t>(0,item.st_size)),stat_key(path,item)});
            }
        } else if (import_name(top) && S_ISREG(st.st_mode) && st.st_uid==geteuid()) s.items.push_back({top,static_cast<uint64_t>(std::max<off_t>(0,st.st_size)),stat_key(top,st)});
    }
    std::sort(s.items.begin(),s.items.end(),[](const Entry&a,const Entry&b){return a.path<b.path;});
    for(const auto& x:s.items) fp+=x.path+":"+std::to_string(x.bytes)+":"+x.fingerprint;
    s.fingerprint=hex64(fp); return s;
}
inline Json plan(Store& store) {
    auto s=scan(store); require(s.items.size()<=128,"cleanup_plan_too_many_items"); Json items=Json::array(); uint64_t total=0;
    for(const auto& x:s.items){ items.push_back({{"id",x.path},{"bytes",x.bytes}}); total+=x.bytes; }
    return {{"schema",1},{"kind","module_cleanup_plan"},{"token",s.fingerprint},{"items",items},{"bytes",total},
        {"protected",{{"game_resources","all_retained"},{"prepared_candidate","retained"}}}};
}
inline Json apply(Store& store,const std::string& token) {
    auto before=scan(store); require(token==before.fingerprint,"cleanup_preview_changed");
    // Preflight every item before deleting any item.
    for(const auto& e:before.items){ auto slash=e.path.find('/'); int parent=store.directory.value, raw=-1;
        std::string name=e.path; if(slash!=e.path.npos){ raw=openat(parent,e.path.substr(0,slash).c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW); require(raw>=0,"cleanup_preview_changed"); parent=raw; name=e.path.substr(slash+1); }
        bool ok=current_fingerprint(parent,e); if(raw>=0) close(raw); require(ok,"cleanup_preview_changed");
    }
    Json state={{"schema",1},{"kind","module_cleanup_result"},{"phase","working"},{"deleted",Json::array()}};
    store.write_document("cleanup-state.json",state);
    try {
        for(const auto& e:before.items){ auto slash=e.path.find('/'); int parent=store.directory.value, raw=-1; std::string name=e.path;
            if(slash!=e.path.npos){raw=openat(parent,e.path.substr(0,slash).c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);require(raw>=0,"cleanup_identity_changed");parent=raw;name=e.path.substr(slash+1);}
            bool identity=current_fingerprint(parent,e); struct stat st{}; identity=identity&&!fstatat(parent,name.c_str(),&st,AT_SYMLINK_NOFOLLOW);
            bool removed=identity&&remove_tree(parent,name,st); if(raw>=0)close(raw);
            require(removed,"cleanup_identity_changed");
            state["deleted"].push_back({{"id",e.path},{"bytes",e.bytes}}); store.write_document("cleanup-state.json",state);
        }
        state["phase"]="succeeded"; store.write_document("cleanup-state.json",state); return state;
    } catch (const std::exception& error) { state["phase"]="failed"; state["error"]=error.what(); try{store.write_document("cleanup-state.json",state);}catch(...){} return state; }
}
} // namespace private_cleanup_detail
inline Json cleanup_plan(Store& store) { return private_cleanup_detail::plan(store); }
inline Json cleanup_apply(Store& store,const std::string& token) { return private_cleanup_detail::apply(store,token); }
} // namespace manager
