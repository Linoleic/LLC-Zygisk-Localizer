#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ============================================================================
// Mock Unity Engine / TMPro Object Lifecycle & Font Fallback Simulator
// ============================================================================

namespace {

enum HideFlags : int32_t {
    None = 0,
    HideInHierarchy = 1,
    HideInInspector = 2,
    DontSaveInEditor = 4,
    NotEditable = 8,
    DontSaveInBuild = 16,
    DontUnloadUnusedAsset = 32,
    DontSave = 4 | 16 | 32 // 52
};

struct MockNativeObject {
    uintptr_t native_id = 0;
    std::string name;
    int32_t hide_flags = HideFlags::None;
    bool is_destroyed = false;
};

// Emulates Unity's native C++ object pool (InstanceID -> C++ Native Object)
static std::unordered_map<uintptr_t, MockNativeObject> g_native_object_pool;
static uintptr_t g_next_native_id = 1000;

struct MockUnityObject {
    uintptr_t m_CachedPtr = 0; // Pointer/ID to C++ native counterpart

    bool is_valid() const {
        if (m_CachedPtr == 0) return false;
        auto it = g_native_object_pool.find(m_CachedPtr);
        return it != g_native_object_pool.end() && !it->second.is_destroyed;
    }

    // Emulates Unity's overloaded `operator == (UnityEngine.Object x, null)`
    bool operator==(std::nullptr_t) const {
        return !is_valid();
    }

    bool operator!=(std::nullptr_t) const {
        return is_valid();
    }
};

struct MockFontAsset : public MockUnityObject {
    std::string font_name;
    std::unordered_set<uint32_t> character_lookup_dictionary;
    std::vector<MockFontAsset*> fallback_font_assets;
    bool is_definition_read = false;

    bool has_character(uint32_t codepoint) const {
        return character_lookup_dictionary.find(codepoint) != character_lookup_dictionary.end();
    }

    void read_font_asset_definition(const std::vector<uint32_t>& available_codepoints) {
        character_lookup_dictionary.clear();
        for (uint32_t cp : available_codepoints) {
            character_lookup_dictionary.insert(cp);
        }
        is_definition_read = true;
    }
};

// Emulates Mono/IL2CPP Boehm-GC Handle table
static std::unordered_map<uintptr_t, MockUnityObject*> g_gc_handles;
static uintptr_t g_next_handle_id = 1;

uintptr_t mock_il2cpp_gchandle_new(MockUnityObject* obj, bool pinned) {
    (void)pinned;
    uintptr_t h = g_next_handle_id++;
    g_gc_handles[h] = obj;
    return h;
}

void mock_il2cpp_gchandle_free(uintptr_t h) {
    g_gc_handles.erase(h);
}

// Emulates Unity's Resources.UnloadUnusedAssets() native garbage collection
void mock_unity_unload_unused_assets() {
    for (auto& pair : g_native_object_pool) {
        // If the object does not have DontUnloadUnusedAsset (32), Unity destroys native C++ counterpart
        if ((pair.second.hide_flags & HideFlags::DontUnloadUnusedAsset) == 0) {
            pair.second.is_destroyed = true;
        }
    }
}

// Helper to create a managed Unity Object backed by Native C++ object
MockFontAsset* create_mock_font(const std::string& name) {
    uintptr_t nid = g_next_native_id++;
    MockNativeObject native_obj;
    native_obj.native_id = nid;
    native_obj.name = name;
    native_obj.hide_flags = HideFlags::None;
    native_obj.is_destroyed = false;
    g_native_object_pool[nid] = native_obj;

    MockFontAsset* font = new MockFontAsset();
    font->m_CachedPtr = nid;
    font->font_name = name;
    return font;
}

// Emulates TextMeshPro character resolution via Fallback list
bool tmpro_resolve_character(MockFontAsset* base_font, uint32_t codepoint, std::string& out_resolved_font) {
    assert(base_font != nullptr);
    if (base_font->has_character(codepoint)) {
        out_resolved_font = base_font->font_name;
        return true;
    }

    // Traverse fallback list
    for (MockFontAsset* fallback : base_font->fallback_font_assets) {
        // Critical Unity check: if fallback == null (which invokes operator == checking m_CachedPtr)
        if (*fallback == nullptr) {
            continue; // Skipped because native object is dead/null!
        }
        if (fallback->has_character(codepoint)) {
            out_resolved_font = fallback->font_name;
            return true;
        }
    }

    // Fallback resolution failed -> triggers '\u25A1' tofu box in TMPro!
    out_resolved_font.clear();
    return false;
}

// Emulates inject_fallback_to_all_loaded_fonts
void inject_fallback(MockFontAsset* base_font, MockFontAsset* fallback_font, bool high_priority) {
    if (!base_font || !fallback_font) return;
    if (base_font == fallback_font) return;

    // Check if already present
    for (size_t i = 0; i < base_font->fallback_font_assets.size(); ++i) {
        if (base_font->fallback_font_assets[i] == fallback_font) {
            if (high_priority && i != 0) {
                base_font->fallback_font_assets.erase(base_font->fallback_font_assets.begin() + i);
                base_font->fallback_font_assets.insert(base_font->fallback_font_assets.begin(), fallback_font);
            }
            return;
        }
    }

    if (high_priority) {
        base_font->fallback_font_assets.insert(base_font->fallback_font_assets.begin(), fallback_font);
    } else {
        base_font->fallback_font_assets.push_back(fallback_font);
    }
}

} // namespace

// ============================================================================
// Test Suites
// ============================================================================

static void test_vulnerability_reproduction_without_hide_flags() {
    // 1. Create base game font (e.g. Pretendard-Regular SDF)
    MockFontAsset* pretendard = create_mock_font("Pretendard-Regular SDF");
    pretendard->read_font_asset_definition({ 'A', 'B', '1', '2' }); // No Chinese characters

    // 2. Load Chinese font without HideFlags.DontUnloadUnusedAsset (Original Buggy State)
    MockFontAsset* chinese_font = create_mock_font("SourceHanSansSC-Bold SDF");
    // Chinese codepoint: '连' (0x8FDE), '清' (0x6E05)
    chinese_font->read_font_asset_definition({ 0x8FDE, 0x6E05, 0x4E00 });

    // Inject fallback
    inject_fallback(pretendard, chinese_font, true);
    assert(pretendard->fallback_font_assets.size() == 1);
    assert(pretendard->fallback_font_assets[0] == chinese_font);

    // Initial character resolution works before scene switch / GC
    std::string resolved;
    bool found = tmpro_resolve_character(pretendard, 0x8FDE, resolved);
    assert(found && resolved == "SourceHanSansSC-Bold SDF");

    // 3. Scene switch / Video playback triggers Unity Resources.UnloadUnusedAssets()
    mock_unity_unload_unused_assets();

    // The native C++ object is now destroyed because hideFlags had no DontUnloadUnusedAsset (32)!
    assert(!chinese_font->is_valid());
    assert(*chinese_font == nullptr); // Unity operator == returns true

    // 4. Character resolution now FAILS, reproducing the exact tofu box bug reported by user!
    found = tmpro_resolve_character(pretendard, 0x8FDE, resolved);
    assert(!found); // Fails to resolve, leading to '\u25A1'!

    delete pretendard;
    delete chinese_font;
}

static void test_font_preservation_with_hide_flags_and_gc_handle() {
    // 1. Create base game font
    MockFontAsset* mikodacs = create_mock_font("Mikodacs SDF");
    mikodacs->read_font_asset_definition({ 'K', 'o', 'r', 'e', 'a' });

    // 2. Load Chinese font with fix: HideFlags.DontSave (52) and GC Handle
    MockFontAsset* chinese_font = create_mock_font("SourceHanSansSC-Bold SDF");
    // Protect native C++ object
    g_native_object_pool[chinese_font->m_CachedPtr].hide_flags = HideFlags::DontSave; // 52
    // Protect IL2CPP managed object
    uintptr_t gc_h = mock_il2cpp_gchandle_new(chinese_font, false);
    assert(gc_h != 0);

    // Call ReadFontAssetDefinition explicitly
    chinese_font->read_font_asset_definition({ 0x8FDE, 0x6E05, 0x4E00 });
    assert(chinese_font->is_definition_read);

    // Inject fallback at index 0
    inject_fallback(mikodacs, chinese_font, true);

    // 3. Trigger multiple rounds of scene transition / UnloadUnusedAssets
    mock_unity_unload_unused_assets();
    mock_unity_unload_unused_assets();

    // 4. Native object remains perfectly ALIVE
    assert(chinese_font->is_valid());
    assert(*chinese_font != nullptr);
    assert(g_gc_handles.find(gc_h) != g_gc_handles.end());

    // 5. TextMeshPro character lookup succeeds without any tofu box!
    std::string resolved;
    bool found_lian = tmpro_resolve_character(mikodacs, 0x8FDE, resolved);
    assert(found_lian && resolved == "SourceHanSansSC-Bold SDF");

    bool found_qing = tmpro_resolve_character(mikodacs, 0x6E05, resolved);
    assert(found_qing && resolved == "SourceHanSansSC-Bold SDF");

    mock_il2cpp_gchandle_free(gc_h);
    delete mikodacs;
    delete chinese_font;
}

static void test_dynamic_scene_fonts_fallback_injection() {
    MockFontAsset* chinese_font = create_mock_font("SourceHanSansSC-Bold SDF");
    chinese_font->read_font_asset_definition({ 0x8FDE, 0x6E05 });
    g_native_object_pool[chinese_font->m_CachedPtr].hide_flags = HideFlags::DontSave;

    // Simulate startup fonts
    MockFontAsset* startup_font = create_mock_font("BebasKai SDF");
    inject_fallback(startup_font, chinese_font, true);
    assert(startup_font->fallback_font_assets.size() == 1);

    // Re-injection must be idempotent and preserve priority at index 0
    inject_fallback(startup_font, chinese_font, true);
    assert(startup_font->fallback_font_assets.size() == 1);
    assert(startup_font->fallback_font_assets[0] == chinese_font);

    // Simulate dynamically loaded lobby font (e.g. newly instantiated LiberationSans in Lobby)
    MockFontAsset* dynamic_lobby_font = create_mock_font("LiberationSans SDF");
    // Initially not patched
    assert(dynamic_lobby_font->fallback_font_assets.empty());

    // Periodic / scene lifecycle scheduler triggers fallback injection
    inject_fallback(dynamic_lobby_font, chinese_font, true);
    assert(dynamic_lobby_font->fallback_font_assets.size() == 1);
    assert(dynamic_lobby_font->fallback_font_assets[0] == chinese_font);

    std::string resolved;
    assert(tmpro_resolve_character(dynamic_lobby_font, 0x8FDE, resolved));
    assert(resolved == "SourceHanSansSC-Bold SDF");

    delete startup_font;
    delete dynamic_lobby_font;
    delete chinese_font;
}

int main() {
    test_vulnerability_reproduction_without_hide_flags();
    test_font_preservation_with_hide_flags_and_gc_handle();
    test_dynamic_scene_fonts_fallback_injection();

    std::cout << "font_lifecycle: vulnerability reproduction, hide_flags preservation, GC handle, and dynamic fallback injection passed" << std::endl;
    return 0;
}
