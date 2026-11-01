"""
Auto-Registry: Metadata-Driven Localization Table Inference & Auditing Engine for Limbus Company.
Leverages exported IL2CPP metadata (llc_metadata_probe.json) to:
1. Discover all TextDataSet localization fields without hardcoded offsets.
2. Resolve corresponding TextData_* classes and their managed string properties.
3. Classify table architectures (SingleString, MultiString, ComplexNested, SpecialRoot).
4. Audit C++ core_main.cpp registry coverage and detect any schema shifts or gaps.
5. Generate declarative C++ Auto-Registry registrations for new/unadapted tables.
"""

import json
import os
import re
import argparse

PROBE_FILE = 'dist/research/llc_metadata_probe.json'
CORE_MAIN_FILE = 'jni/core/core_main.cpp'

def build_registry():
    if not os.path.exists(PROBE_FILE):
        print(f"[-] Error: {PROBE_FILE} does not exist.")
        return None

    data = json.load(open(PROBE_FILE, encoding='utf-8'))
    classes = {c.get('name'): c for c in data.get('classes', [])}

    tds = classes.get('TextDataSet')
    if not tds:
        print("[-] Error: TextDataSet not found in probe.")
        return None

    fields = tds.get('fields', [])
    registry = []

    for f in fields:
        name = f.get('name')
        offset = f.get('offset')
        raw_type = f.get('type', '')

        # Filter out internal/static non-table fields
        if offset == 0 or name in ('_converter', '_isLoadedRemote', 'LOCALIZE_FOLDER_PATH',
                                  'LOCAL_LOCALIZEFILE_LIST_NAME', 'REMOTE_LOCALIZEFILE_LIST_NAME',
                                  'REMOTE_LOCALIZE_FOLDER_LABEL'):
            continue

        # Extract inner item class from JsonDataList<X> or raw type
        m = re.search(r'<(?:.*?\.)?(\w+)>', raw_type)
        target_class = m.group(1) if m else raw_type

        cls_info = classes.get(target_class)
        string_fields = []
        nested_fields = []

        if cls_info:
            for cf in cls_info.get('fields', []):
                cf_name = cf.get('name')
                cf_off = cf.get('offset')
                cf_type = cf.get('type', '')
                if cf_off == 0:
                    continue  # static field
                if cf_name.startswith('_isCustomLang'):
                    continue  # custom runtime flag

                # Check if nested list / array / dict
                if any(x in cf_type for x in ('[]', 'List', 'Dictionary')) or any(k in cf_name.lower() for k in ('list', 'options', 'dict')):
                    nested_fields.append((cf_name, cf_off, cf_type))
                else:
                    string_fields.append((cf_name, cf_off))

        # Categorization
        if nested_fields:
            cat = "ComplexNested"
        elif not cls_info or not string_fields:
            cat = "SpecialRoot"
        elif len(string_fields) == 1 and string_fields[0][0] in ('content', 'parttitle', 'name'):
            cat = "SingleString"
        else:
            cat = "MultiString"

        registry.append({
            "name": name,
            "runtime_offset": offset,
            "raw_type": raw_type,
            "target_class": target_class,
            "category": cat,
            "string_fields": string_fields,
            "nested_fields": nested_fields
        })

    return registry

def audit_cpp_registry(registry):
    if not os.path.exists(CORE_MAIN_FILE):
        print(f"[-] Error: {CORE_MAIN_FILE} does not exist.")
        return

    with open(CORE_MAIN_FILE, 'r', encoding='utf-8') as f:
        core_src = f.read()

    print("\n=======================================================")
    print("       AUTO-REGISTRY AUDIT: C++ RUNTIME COVERAGE        ")
    print("=======================================================\n")

    adapted_count = 0
    missing_count = 0
    missing_by_cat = {}

    for entry in registry:
        name = entry['name']
        # Check if table name or alias is registered in core_main.cpp
        is_registered = (f'"{name}"' in core_src)
        if not is_registered and name.endswith('List'):
            base = name[:-4]
            is_registered = (f'"{base}"' in core_src)
        elif not is_registered:
            alt = name + "List"
            is_registered = (f'"{alt}"' in core_src)

        if is_registered:
            adapted_count += 1
        else:
            missing_count += 1
            missing_by_cat.setdefault(entry['category'], []).append(entry)

    print(f"[+] Total TextDataSet content fields : {len(registry)}")
    print(f"[+] Registered in core_main.cpp     : {adapted_count} (active in runtime)")
    print(f"[-] Unregistered / Candidate fields  : {missing_count}\n")

    for cat, items in missing_by_cat.items():
        print(f"--- Unregistered {cat} ({len(items)}) ---")
        for item in items:
            print(f"  {item['name']:<35} class={item['target_class']:<28} offset={item['runtime_offset']}")
        print()

def main():
    parser = argparse.ArgumentParser(description="Auto-Registry Metadata Inference Engine")
    parser.add_argument("--summary", action="store_true", help="Print summary of inferred tables")
    parser.add_argument("--audit", action="store_true", help="Audit C++ core_main.cpp registered coverage against metadata probe")
    parser.add_argument("--codegen", action="store_true", help="Generate C++ target table definitions")
    args = parser.parse_args()

    reg = build_registry()
    if not reg: return

    if args.audit or not (args.summary or args.codegen):
        audit_cpp_registry(reg)

    if args.summary:
        print(f"\n[+] Auto-Registry Analysis: {len(reg)} active content fields discovered on TextDataSet\n")
        categories = {}
        for entry in reg:
            categories.setdefault(entry['category'], []).append(entry)

        for cat in ['SingleString', 'MultiString', 'ComplexNested', 'SpecialRoot']:
            items = categories.get(cat, [])
            print(f"=== {cat} ({len(items)} tables) ===")
            for item in items:
                f_desc = ", ".join([f"{n}@{o}" for n, o in item['string_fields']]) if item['string_fields'] else ""
                n_desc = ", ".join([f"{n}@{o}[{t}]" for n, o, t in item['nested_fields']]) if item['nested_fields'] else ""
                details = []
                if f_desc: details.append(f"fields=[{f_desc}]")
                if n_desc: details.append(f"nested=[{n_desc}]")
                detail_str = "; ".join(details) if details else "no fields"
                print(f"  {item['name']:<35} class={item['target_class']:<30} {detail_str}")
            print()

    if args.codegen:
        print("\n=== Generated Declarative Auto-Registry Declarations ===")
        for entry in reg:
            if entry['category'] == 'SingleString' and entry['target_class'] == 'TextData_UI':
                print(f'    AUTO_TARGET("{entry["name"]}", patch_ui_table),')
            elif entry['category'] == 'MultiString':
                print(f'    AUTO_TARGET("{entry["name"]}", patch_{entry["name"].strip("_").lower()}_list),')

if __name__ == '__main__':
    main()
