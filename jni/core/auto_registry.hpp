#pragma once
#include "string_table_patcher.hpp"
#include "content_sync.hpp"
#include <string>
#include <string_view>
#include <unordered_map>

/**
 * Auto-Registry: Metadata-driven declarative localization registry macros and helpers.
 *
 * Eliminates repetitive boilerplate functions in data_patcher.cpp and removes
 * brittle hardcoded byte offsets in core_main.cpp.
 */

// ----------------------------------------------------------------------------
// Patch Function Declarative Generators
// ----------------------------------------------------------------------------

// Single-string UI patcher: patches content@24 of TextData_UI
#define DEFINE_UI_TABLE_PATCH(func_name, map_var) \
    int func_name(Il2CppObject* container) { \
        return string_table::patch(container, map_var, "TextData_UI", "content", true /* numeric_ids */, true /* allow_unidentified_rows */); \
    }

// Single-string custom class patcher: patches specified field of expected_class
#define DEFINE_SINGLE_STRING_TABLE_PATCH(func_name, map_var, class_name, field_name, numeric_ids, allow_unid) \
    int func_name(Il2CppObject* container) { \
        return string_table::patch(container, map_var, class_name, field_name, numeric_ids, allow_unid); \
    }

// Two-field entity patcher (e.g. name, desc)
#define DEFINE_ENTITY_2_TABLE_PATCH(func_name, map_var, class_name, f1, f2, numeric_ids, allow_unid) \
    int func_name(Il2CppObject* container) { \
        return string_table::visit(container, class_name, [&](Il2CppObject* row, const std::string& key) { \
            auto it = map_var.find(key); \
            if (it == map_var.end()) return 0; \
            int count = 0; \
            if (!it->second.name.empty()) { \
                if (!write_managed_text_named(row, f1, it->second.name)) return -1; \
                count++; \
            } \
            if (!it->second.desc.empty()) { \
                if (!write_managed_text_named(row, f2, it->second.desc)) return -1; \
                count++; \
            } \
            return count ? 1 : 0; \
        }, numeric_ids, allow_unid); \
    }

// ----------------------------------------------------------------------------
// Declarative ContentTarget Registrars
// ----------------------------------------------------------------------------

// Auto-reflected target with remote gate enabled (default for game data tables)
#define AUTO_TARGET(field_name, patch_fn) \
    { field_name, 0, patch_fn, {}, true /* requires_remote */, true /* reflected_container */ }

// Auto-reflected target without remote gate (for early loading hints, tags)
#define AUTO_TARGET_NO_REMOTE(field_name, patch_fn) \
    { field_name, 0, patch_fn, {}, false /* requires_remote */, true /* reflected_container */ }

// Legacy fallback target with explicit offset and reflection mode
#define AUTO_TARGET_LEGACY(field_name, byte_offset, patch_fn, req_remote, reflected) \
    { field_name, byte_offset, patch_fn, {}, req_remote, reflected }
