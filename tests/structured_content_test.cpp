#include "content_readback.hpp"
#include <cassert>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

struct Il2CppType { Il2CppClass* klass; };
struct Field { uint32_t offset; const char* name; Il2CppType type; };
struct Il2CppClass { const char* name; Il2CppClass* parent; std::vector<Field*> fields; bool value_type = false; std::vector<MethodInfo*> methods; };
struct Il2CppObject { Il2CppClass* klass; void* monitor = nullptr; };
struct Il2CppString { Il2CppObject object; std::u16string text; };
struct Box { Il2CppObject object; alignas(8) unsigned char data[128]{}; };
struct Entry { Il2CppObject object; int32_t hash; int32_t pad = 0; Il2CppString* key; Il2CppObject* value; };

static std::unordered_map<uintptr_t, Il2CppObject*> handles;
static std::unordered_map<void*, uintptr_t> array_lengths;
static std::vector<std::unique_ptr<Il2CppString>> strings;
static Il2CppType* int_param;
static Il2CppClass *int_class, *string_class, *list_class;

static Il2CppString* str(const char* value) {
    auto item = std::make_unique<Il2CppString>(); item->object.klass = string_class;
    while (*value) item->text.push_back(static_cast<unsigned char>(*value++));
    auto* result = item.get(); strings.push_back(std::move(item)); return result;
}
static Box* box(Il2CppClass* klass) { auto* b = new Box{}; b->object.klass = klass; return b; }
static void setptr(Box* b, uint32_t offset, void* p) { memcpy(reinterpret_cast<char*>(b) + offset, &p, sizeof(p)); }
static void setint(Box* b, uint32_t offset, int32_t n) { memcpy(reinterpret_cast<char*>(b) + offset, &n, 4); }
static void settext(Box* b, uint32_t offset, const char* s) { setptr(b, offset, str(s)); }
static Il2CppObject* ref(Box* b, uint32_t offset) { Il2CppObject* p = nullptr; memcpy(&p, reinterpret_cast<char*>(b) + offset, sizeof(p)); return p; }
static std::u16string value(Box* b, uint32_t offset) { auto* s = reinterpret_cast<Il2CppString*>(ref(b, offset)); return s ? s->text : u""; }
static Field* fld(Il2CppClass& k, uint32_t offset, const char* name, Il2CppClass* type) {
    auto* f = new Field{offset, name, {type}}; k.fields.push_back(f); return f;
}
static Box* row(Il2CppClass* klass, int id, const char* id_field, Il2CppClass* id_type,
                const std::vector<std::pair<const char*, const char*>>& texts,
                const std::vector<std::pair<const char*, Il2CppObject*>>& lists = {},
                const std::vector<std::pair<const char*, int>>& ints = {}) {
    auto* b = box(klass); uint32_t offset = 16;
    if (id_type == int_class) setint(b, offset, id); else settext(b, offset, std::to_string(id).c_str());
    fld(*klass, offset, id_field, id_type); offset += 8;
    for (auto [name, text] : texts) { fld(*klass, offset, name, string_class); settext(b, offset, text); offset += 8; }
    for (auto [name, list] : lists) { fld(*klass, offset, name, list_class); setptr(b, offset, list); offset += 8; }
    for (auto [name, n] : ints) { fld(*klass, offset, name, int_class); setint(b, offset, n); offset += 8; }
    return b;
}

static Il2CppClass integer{"Int32", nullptr, {}, true, {}}, text{"String", nullptr, {}, false, {}}, list_type{"List`1", nullptr, {}, false, {}};
static Il2CppClass entry_type{"Entry", nullptr, {}, true, {}}, entries_type{"Entry[]", nullptr, {}, false, {}};
static MethodInfo get_value{};

static Box* managed_list(const std::vector<Il2CppObject*>& objects) {
    auto* list = box(&list_type);
    // IL2CPP reference array: object header, bounds, max_length, elements at +32.
    auto* raw = new uintptr_t[4 + objects.size()]{}; raw[0] = reinterpret_cast<uintptr_t>(&entries_type); raw[3] = objects.size();
    for (size_t i = 0; i < objects.size(); ++i) raw[4 + i] = reinterpret_cast<uintptr_t>(objects[i]);
    auto* array = reinterpret_cast<Il2CppObject*>(raw); array_lengths[array] = objects.size();
    setptr(list, 16, array); setint(list, 24, static_cast<int32_t>(objects.size())); setint(list, 28, static_cast<int32_t>(objects.size()));
    fld(list_type, 16, "_items", &entries_type); fld(list_type, 24, "_size", &integer); fld(list_type, 28, "_version", &integer);
    return list;
}
static Box* table(const char* row_class_name, const std::vector<std::pair<std::string, Box*>>& rows) {
    auto* rowklass = new Il2CppClass{row_class_name, nullptr, {}, false, {}};
    // Row metadata is already attached to the concrete runtime class.
    if (!rows.empty()) rowklass = rows.front().second->object.klass;
    std::vector<Entry*> slots;
    int n = 0;
    for (auto& [key, r] : rows) {
        auto* e = new Entry{}; e->object.klass = &entry_type; e->hash = n++; e->key = str(key.c_str()); e->value = &r->object; slots.push_back(e);
    }
    if (entry_type.fields.empty() && !rows.empty()) {
        fld(entry_type, 16, "hashCode", &integer); fld(entry_type, 24, "key", &text);
        fld(entry_type, 32, "value", rows.front().second->object.klass);
    }
    auto* raw = new uintptr_t[4 + slots.size()]{}; raw[0] = reinterpret_cast<uintptr_t>(&entries_type); raw[3] = slots.size();
    for (size_t i = 0; i < slots.size(); ++i) raw[4 + i] = reinterpret_cast<uintptr_t>(slots[i]);
    auto* array = reinterpret_cast<Il2CppObject*>(raw); array_lengths[array] = slots.size();
    auto* dict = box(new Il2CppClass{"Dictionary`2", nullptr, {}, false, {}});
    auto* dk = dict->object.klass; fld(*dk, 16, "_count", &integer); fld(*dk, 24, "_entries", &entries_type);
    setint(dict, 16, static_cast<int32_t>(slots.size())); setptr(dict, 24, array);
    auto* root = box(new Il2CppClass{"JsonDataList`1", nullptr, {}, false, {}});
    fld(*root->object.klass, 16, "_dic", dk); setptr(root, 16, &dict->object); (void)rowklass;
    return root;
}

int main() {
    int_class = &integer; string_class = &text; list_class = &list_type;
    Il2CppType int_type{&integer}; int_param = &int_type;
    il2cpp_object_get_class_fn = [](Il2CppObject* o) { return o->klass; };
    il2cpp_class_get_name_fn = [](Il2CppClass* k) { return k->name; };
    il2cpp_class_get_parent_fn = [](Il2CppClass* k) { return k->parent; };
    il2cpp_class_is_valuetype_fn = [](const Il2CppClass* k) { return k->value_type; };
    il2cpp_class_get_fields_fn = [](Il2CppClass* k, void** it) -> void* { size_t i = reinterpret_cast<uintptr_t>(*it); if (i >= k->fields.size()) return nullptr; *it = reinterpret_cast<void*>(i + 1); return k->fields[i]; };
    il2cpp_class_get_field_from_name_fn = [](Il2CppClass* k, const char* name) -> void* { for (auto* f : k->fields) if (!strcmp(f->name, name)) return f; return nullptr; };
    il2cpp_field_get_flags_fn = [](void*) { return 0; };
    il2cpp_field_get_name_fn = [](void* f) { return static_cast<Field*>(f)->name; };
    il2cpp_field_get_offset_fn = [](void* f) { return static_cast<Field*>(f)->offset; };
    il2cpp_field_get_type_fn = [](void* f) -> const Il2CppType* { return &static_cast<Field*>(f)->type; };
    il2cpp_class_from_type_fn = [](const Il2CppType* t) { return t->klass; };
    il2cpp_field_get_value_fn = [](Il2CppObject* o, void* f, void* out) { auto* field = static_cast<Field*>(f); memcpy(out, reinterpret_cast<char*>(o) + field->offset, field->type.klass == &integer ? 4 : sizeof(void*)); };
    il2cpp_string_length_fn = [](Il2CppString* s) { return static_cast<int32_t>(s->text.size()); };
    il2cpp_string_chars_fn = [](Il2CppString* s) { return reinterpret_cast<const uint16_t*>(s->text.c_str()); };
    il2cpp_string_new_fn = [](const char* s) { return str(s); };
    il2cpp_gc_wbarrier_set_field_fn = [](Il2CppObject*, void** p, void* v) { *p = v; };
    il2cpp_gchandle_new_fn = [](Il2CppObject* o, bool) { static uintptr_t h = uintptr_t{1} << 40; handles[++h] = o; return h; };
    il2cpp_gchandle_get_target_fn = [](uintptr_t h) { return handles.at(h); };
    il2cpp_gchandle_free_fn = [](uintptr_t h) { handles.erase(h); };
    il2cpp_array_length_fn = [](void* a) { return array_lengths.at(a); };
    il2cpp_class_get_methods_fn = [](Il2CppClass* k, void** it) -> const MethodInfo* { size_t i = reinterpret_cast<uintptr_t>(*it); if (i >= k->methods.size()) return nullptr; *it = reinterpret_cast<void*>(i + 1); return k->methods[i]; };
    il2cpp_method_get_name_fn = [](const MethodInfo*) { return "GetValue"; };
    il2cpp_method_get_param_count_fn = [](const MethodInfo*) { return 1u; };
    il2cpp_method_get_param_fn = [](const MethodInfo*, uint32_t) -> const Il2CppType* { return int_param; };
    il2cpp_runtime_invoke_fn = [](const MethodInfo*, void* a, void** args, Il2CppException**) -> Il2CppObject* { auto index = *static_cast<int32_t*>(args[0]); auto* raw = static_cast<uintptr_t*>(a); return reinterpret_cast<Il2CppObject*>(raw[4 + index]); };
    entries_type.methods.push_back(&get_value);

    // Skills: source order differs from runtime order; a missing level is retained and reported.
    auto* skillclass = new Il2CppClass{"TextData_Skill", nullptr, {}, false, {}};
    auto* levelclass = new Il2CppClass{"TextData_SkillPerLevel", nullptr, {}, false, {}};
    auto make_level = [&](int lv, const char* name) { auto* r = box(levelclass); fld(*levelclass, 16, "level", &integer); fld(*levelclass, 24, "name", &text); setint(r,16,lv); settext(r,24,name); return r; };
    auto* l4 = make_level(4,"old4"), *l1 = make_level(1,"old1"), *l2 = make_level(2,"old2"), *l3 = make_level(3,"old3");
    auto* levels = managed_list({&l4->object,&l1->object,&l2->object,&l3->object});
    auto* sk = row(skillclass, 17,"id",&integer,{},{{"levelList",&levels->object}});
    auto* skilltable = table("TextData_Skill", {{"17",sk}});
    SkillEntry se; se.id=17;
    SkillLevel s1; s1.level=1; s1.name="one"; SkillLevel s2; s2.level=2; s2.name="two"; SkillLevel s4; s4.level=4; s4.name="four";
    se.levelList={s1,s2,s4}; g_skills[17]=se;
    translation_evidence::Scope scope("_skillList"); assert(structured_content::skills(&skilltable->object)==1);
    assert(value(l1,24)==u"one" && value(l2,24)==u"two" && value(l4,24)==u"four" && value(l3,24)==u"old3");
    assert(!scope.attempt.structure_issues.empty() && scope.attempt.writes == 3 && handles.empty());
    // Runtime duplicate IDs are ambiguous and must preserve both originals.
    setint(l2,16,1); auto* before1=value(l1,24).empty()?nullptr:ref(l1,24); auto* before2=ref(l2,24);
    translation_evidence::Scope dup_scope("_skillList"); structured_content::skills(&skilltable->object);
    assert(ref(l1,24)==before1 && ref(l2,24)==before2 && !dup_scope.attempt.structure_issues.empty() && handles.empty());
    setint(l2,16,2);

    // Coin description count mismatch preserves the entire anonymous subtree, while level text still writes.
    auto* coinclass = new Il2CppClass{"TextData_Skill_Coins",nullptr,{},false,{}};
    auto* descclass = new Il2CppClass{"TextData_Skill_CoinDesc",nullptr,{},false,{}};
    auto* desc = row(descclass,0,"unused",int_class,{{"desc","coin original"},{"summary","summary original"}});
    auto* descs = managed_list({&desc->object});
    auto* coin = box(coinclass); fld(*coinclass,16,"coindescs",list_class); setptr(coin,16,&descs->object);
    auto* coins = managed_list({&coin->object});
    auto* lc = make_level(1,"coin original name"); fld(*levelclass,32,"coinlist",list_class); setptr(lc,32,&coins->object);
    SkillEntry coin_source; coin_source.id=18; SkillLevel cl; cl.level=1; cl.name="translated level name";
    SkillCoin source_coin; source_coin.coindescs={{"translated coin","translated summary"},{"extra","extra"}};
    cl.coinlist={source_coin,source_coin}; coin_source.levelList={cl}; g_skills[18]=coin_source;
    auto* skill2 = row(skillclass,18,"id",int_class,{},{{"levelList",&managed_list({&lc->object})->object}});
    auto* skilltable2=table("TextData_Skill",{{"18",skill2}});
    translation_evidence::Scope coin_scope("_skillList"); assert(structured_content::skills(&skilltable2->object)==1);
    assert(value(lc,24)==u"translated level name" && value(desc,24)==u"coin original" && value(desc,32)==u"summary original");
    assert(coin_scope.attempt.structure_issues["skill_coin_count_mismatch"] == 1 && handles.empty());

    // Gift alignment by ability ID, duplicate source/runtime groups by occurrence, and zero placeholder.
    auto* giftclass = new Il2CppClass{"TextData_EgoGift",nullptr,{},false,{}};
    auto* abilityclass = new Il2CppClass{"TextData_EgoGiftAbility",nullptr,{},false,{}};
    auto ability_row = [&](int id,const char* original) { auto* r=box(abilityclass); fld(*abilityclass,16,"abilityID",&integer); fld(*abilityclass,24,"simpleDesc",&text); setint(r,16,id); settext(r,24,original); return r; };
    auto* a2=ability_row(2,"a2"), *a1x=ability_row(1,"dup1"), *az=ability_row(0,"placeholder"), *a1y=ability_row(1,"dup2");
    auto* al=managed_list({&a2->object,&a1x->object,&az->object,&a1y->object});
    auto* gift=row(giftclass,0,"id",string_class,{{"name","gift"},{"desc","original effect"}},{{"simpleDesc",&al->object}});
    // String keys and row IDs must agree; construct ID explicitly.
    settext(gift,16,"g"); auto* gt=table("TextData_EgoGift",{{"g",gift}});
    EgoGiftEntry ge; ge.name="gift translated"; ge.desc="translated full effect"; ge.simpleDesc={{1,"first"},{2,"second-id"},{1,"second"}}; g_ego_gifts["g"]=ge;
    translation_evidence::Scope gs("_egoGiftList"); structured_content::gifts(&gt->object);
    assert(value(a2,24)==u"second-id" && value(a1x,24)==u"first" && value(a1y,24)==u"second" && value(az,24)==u"placeholder" && handles.empty());
    assert(value(gift,32)==u"translated full effect");
    settext(gift,32,"new original effect"); auto* original_desc=ref(gift,32);
    auto* original_x=ref(a1x,24); auto* original_y=ref(a1y,24);
    g_ego_gifts["g"].simpleDesc={{1,"only one duplicate"},{2,"still translated"}};
    translation_evidence::Scope gm("_egoGiftList"); structured_content::gifts(&gt->object);
    assert(ref(a1x,24)==original_x && ref(a1y,24)==original_y && value(a2,24)==u"still translated" &&
           !gm.attempt.structure_issues.empty() && handles.empty());
    assert(ref(gift,32)==original_desc && gm.attempt.retained_original_fields == 1);
    content_readback::Check gift_counts; gift_counts.gift(&gift->object, "g", g_ego_gifts["g"]);
    assert(gift_counts.structures == 2 && gift_counts.examples[0]["reason"] == "ability_count_mismatch");
    assert(gift_counts.examples[0]["runtime_count"] == 2 && gift_counts.examples[0]["source_count"] == 1);
    assert(gift_counts.examples[0]["runtime_abilities"].size() == 2 && gift_counts.examples[0]["source_abilities"].size() == 2);
    EgoGiftEntry missing_ability = g_ego_gifts["g"]; missing_ability.simpleDesc = {{2,"still translated"}};
    content_readback::Check gift_missing; gift_missing.gift(&gift->object, "g", missing_ability);
    assert(gift_missing.structures == 0 && gift_missing.missing_subrows == 2 && gift_missing.retained == 1);
    assert(gift_missing.examples[0]["reason"] == "ability_missing_local" && gift_missing.examples[0]["source_count"] == 0);
    assert(gift_missing.examples[0]["source_abilities"].size() == 1);
    assert(ref(a1x,24)==original_x && ref(a1y,24)==original_y && handles.empty());
    g_ego_gifts["g"] = missing_ability;
    { translation_evidence::Scope gap("_egoGiftData"); structured_content::gifts(&gt->object);
      assert(gap.attempt.structure_issues.empty() && gap.attempt.missing_local_subrows==2 && gap.attempt.retained_original_fields==1);
      assert(gap.attempt.json()["missing_subrow_examples"].size()==1 && ref(gift,32)==original_desc); }
    for (int i = 0; i < 10; ++i) gift_missing.gift(&gift->object, "g", missing_ability);
    assert(gift_missing.examples.size() == 12);
    auto long_text = std::string(300, 'x'); settext(a1x,24,long_text.c_str());
    auto sample = content_readback::Check::text_sample(&a1x->object,"simpleDesc");
    assert(sample["truncated"] == true && sample["text"].get<std::string>().size() == 256);
    setptr(a1x,24,original_x);
    translation_evidence::Attempt retained_only;
    retained_only.rows["g"] = {translation_evidence::RowState::Available};
    retained_only.rows["g"].retained_fields = 1;
    assert(!retained_only.operation_failed() && retained_only.json()["available_rows_without_write"] == 0);

    // Notes: ID keyed fields and empty source title do not clear runtime text.
    auto* noteclass=new Il2CppClass{"TextData_DanteNote",nullptr,{},false,{}};
    auto* note=row(noteclass,0,"id",string_class,{{"title","runtime title"},{"desc","runtime desc"}});
    settext(note,16,"note"); auto* nt=table("TextData_DanteNote",{{"note",note}});
    g_dante_notes["note"]={"","translated desc"}; translation_evidence::Scope ns("_danteNoteList");
    assert(structured_content::notes(&nt->object)==1 && value(note,24)==u"runtime title" && value(note,32)==u"translated desc" && handles.empty());

    // Notebook navigation creates numeric-key rows with an empty model ID.
    // Do not invent a source association from their keys or call these a failed write.
    auto* unidentified_note=row(noteclass,0,"id",string_class,{{"title","keep title"},{"desc","keep desc"}});
    settext(unidentified_note,16,"");
    auto* dynamic_notes=table("TextData_DanteNote",{{"note",note},{"94",unidentified_note}});
    g_dante_notes["94"]={"wrong guessed title","wrong guessed desc"};
    translation_evidence::Scope dynamic_scope("_danteNoteDesc");
    assert(structured_content::notes(&dynamic_notes->object)==1);
    assert(dynamic_scope.attempt.unidentified_rows==1 && dynamic_scope.attempt.invalid_rows==0 &&
           !dynamic_scope.attempt.operation_failed() && value(unidentified_note,24)==u"keep title" &&
           value(unidentified_note,32)==u"keep desc" && handles.empty());
    g_dante_notes.erase("94");

    // A list whose declared size exceeds its backing array and a row of the wrong category are skipped safely.
    auto* broken_list=managed_list({&l1->object}); setint(broken_list,24,2);
    auto* broken_skill=row(skillclass,17,"id",int_class,{},{{"levelList",&broken_list->object}});
    auto* broken_table=table("TextData_Skill",{{"17",broken_skill}});
    auto* original_skill_name=ref(l1,24);
    translation_evidence::Scope bounds_scope("_skillList"); assert(structured_content::skills(&broken_table->object)==1);
    assert(ref(l1,24)==original_skill_name && !bounds_scope.attempt.structure_issues.empty() && handles.empty());
    auto* wrong_class=new Il2CppClass{"TextData_NotASkill",nullptr,{},false,{}};
    auto* wrong=row(wrong_class,17,"id",int_class,{{"name","untouched"}});
    auto* wrong_table=table("TextData_Skill",{{"17",wrong}});
    translation_evidence::Scope category_scope("_skillList"); assert(structured_content::skills(&wrong_table->object)==0);
    assert(category_scope.attempt.invalid_rows == 1 && handles.empty());

    // Numeric ID zero is a sentinel; an empty key is unidentified, not an observed missing translation.
    auto* string_id_skillclass=new Il2CppClass{"TextData_Skill",nullptr,{},false,{}};
    auto* real17=row(string_id_skillclass,17,"id",string_class,{{"name","keep17"}}); settext(real17,16,"17");
    auto* zero=row(string_id_skillclass,0,"id",string_class,{{"name","keep0"}}); settext(zero,16,"0");
    auto* empty=row(string_id_skillclass,0,"id",string_class,{{"name","keep-empty"}}); settext(empty,16,"");
    auto* sentinel_table=table("TextData_Skill",{{"17",real17},{"0",zero},{"",empty}});
    auto saved_skill=g_skills.at(17); g_skills.erase(17);
    translation_evidence::Scope sentinel_scope("_skillList");
    assert(structured_content::skills(&sentinel_table->object)==0);
    assert(sentinel_scope.attempt.rows.count("17") == 1 && sentinel_scope.attempt.rows.count("0") == 0);
    assert(sentinel_scope.attempt.unidentified_rows == 1 && value(real17,24)==u"keep17" &&
           value(zero,24)==u"keep0" && value(empty,24)==u"keep-empty" && handles.empty());
    g_skills[17]=saved_skill;
    // Separate flat tables with the same ID must retain their own translations.
    auto* bufclass=new Il2CppClass{"TextData_Buf",nullptr,{},false,{}};
    auto* kwclass=new Il2CppClass{"TextData_BattleKeywords",nullptr,{},false,{}};
    auto* buf=row(bufclass,0,"id",string_class,{{"name","old buf"},{"desc","old desc"},{"summary","old summary"},{"flavor","old flavor"}});
    auto* kw=row(kwclass,0,"id",string_class,{{"name","old keyword"},{"desc","old desc"},{"flavor","old flavor"}});
    settext(buf,16,"shared"); settext(kw,16,"shared");
    auto* anonymous_buf=row(bufclass,0,"id",string_class,{{"name","keep placeholder"}});
    settext(anonymous_buf,16,"");
    auto* bt=table("TextData_Buf",{{"shared",buf},{"dynamic",anonymous_buf}}); auto* kt=table("TextData_BattleKeywords",{{"shared",kw}});
    g_bufs["shared"]={"shared","buf name","buf desc","buf summary","buf flavor"};
    g_bufs["dynamic"]={"dynamic","never guess from key","","",""};
    g_battle_keywords["shared"]={"shared","keyword name","keyword desc","unavailable summary","keyword flavor"};
    translation_evidence::Scope bs("_bufList"); assert(structured_content::flat(&bt->object,false)==1);
    assert(value(buf,24)==u"buf name" && value(buf,40)==u"buf summary" && bs.attempt.writes==4);
    assert(bs.attempt.unidentified_rows==1 && bs.attempt.invalid_rows==0 && value(anonymous_buf,24)==u"keep placeholder");
    translation_evidence::Scope ks("_keywordList"); assert(structured_content::flat(&kt->object,true)==1);
    assert(value(kw,24)==u"keyword name" && value(kw,40)==u"keyword flavor" && ks.attempt.writes==3 &&
           ks.attempt.unsupported_fields["summary"]==1 && !ks.attempt.operation_failed());
    assert(structured_content::flat(&kt->object,false)==0); // A wrong model cannot be overwritten.

    // Readback uses no active translation evidence and never writes game fields.
    auto* datasetclass=new Il2CppClass{"TextDataSet",nullptr,{},false,{}};
    auto* ds=box(datasetclass); fld(*datasetclass,16,"_bufList",bt->object.klass); setptr(ds,16,&bt->object);
    fld(*datasetclass,24,"_keywordList",kt->object.klass); setptr(ds,24,&kt->object);
    fld(*datasetclass,32,"_danteNoteDesc",dynamic_notes->object.klass); setptr(ds,32,&dynamic_notes->object);
    fld(*datasetclass,40,"_egoGiftData",gt->object.klass); setptr(ds,40,&gt->object);
    auto* saved_active=translation_evidence::active; translation_evidence::active=nullptr;
    const auto writes_before=g_managed_text_writes; auto* preserved=ref(kw,24);
    auto gr=content_readback::inspect(&ds->object,"_egoGiftData");
    assert(gr["status"]=="passed" && gr["missing_local_subrows"]==2 && gr["retained_original_fields"]==1 && gr["structure_mismatches"]==0);
    assert(gr["examples"][0]["observed_parent_desc"]["text"]=="new original effect");
    auto br=content_readback::inspect(&ds->object,"_bufList");
    assert(br["status"]=="passed" && br["equal_fields"]==4 && br["snapshot_unchanged"]==true && br["unidentified_runtime_rows"]==1);
    auto kr=content_readback::inspect(&ds->object,"_keywordList");
    assert(kr["status"]=="passed" && kr["equal_fields"]==3 && kr["unsupported_source_fields"]==1);
    settext(buf,32,"late change"); auto changed_readback=content_readback::inspect(&ds->object,"_bufList");
    assert(changed_readback["status"]=="failed" && changed_readback["mismatched_fields"]==1);
    auto nr=content_readback::inspect(&ds->object,"_danteNoteDesc");
    assert(nr["status"]=="passed" && nr["unidentified_runtime_rows"]==1 && nr["equal_fields"]==1);
    assert(content_readback::inspect(&ds->object,"unknown")["status"]=="failed");
    assert(g_managed_text_writes==writes_before && ref(kw,24)==preserved && handles.empty());
    // Candidate inspection may find untranslated content; it must never write it
    // or accept its diagnostic completion as proof that translations were applied.
    auto* uiclass=new Il2CppClass{"TextData_UI",nullptr,{},false,{}};
    auto* candidate=row(uiclass,0,"id",string_class,{{"content","runtime original"}}); settext(candidate,16,"candidate");
    auto* ct=table("TextData_UI",{{"candidate",candidate}});
    fld(*datasetclass,48,"_gachaNotice",ct->object.klass); setptr(ds,48,&ct->object);
    g_gacha_notices["candidate"]="translation hint";
    auto audit=content_readback::inspect(&ds->object,"_gachaNotice");
    assert(audit["status"]=="passed" && audit["candidate_audit"]==true && audit["mapped_hint_rows"]==1 && audit["equal_hint_rows"]==0);
    assert(audit["samples"][0]["observed"]["text"]=="runtime original" && value(candidate,24)==u"runtime original");
    BuildSha256 candidate_ids_hash; const std::string candidate_ids="[\"candidate\"]";
    candidate_ids_hash.update(reinterpret_cast<const uint8_t*>(candidate_ids.data()),candidate_ids.size());
    assert(audit["observed_id_set_sha256"]==candidate_ids_hash.hex_digest());
    auto* empty_candidate=table("TextData_UI",{});
    fld(*datasetclass,56,"<LobbyBGMCategory>k__BackingField",empty_candidate->object.klass); setptr(ds,56,&empty_candidate->object);
    auto empty_audit=content_readback::inspect(&ds->object,"<LobbyBGMCategory>k__BackingField");
    assert(empty_audit["status"]=="passed" && empty_audit["observed_rows"]==0 && empty_audit["traversal_complete"]==true);
    assert(g_managed_text_writes==writes_before && handles.empty());
    // New registered tables must use their own maps even when UI keys collide.
    auto* storyrow=row(uiclass,0,"id",string_class,{{"content","original story"}}); settext(storyrow,16,"shared_text");
    auto* storytable=table("TextData_UI",{{"shared_text",storyrow}});
    auto* illustrow=row(uiclass,0,"id",string_class,{{"content","original condition"}}); settext(illustrow,16,"shared_text");
    auto* illusttable=table("TextData_UI",{{"shared_text",illustrow}});
    fld(*datasetclass,64,"_story",storytable->object.klass); setptr(ds,64,&storytable->object);
    fld(*datasetclass,72,"_illustGetConditionText",illusttable->object.klass); setptr(ds,72,&illusttable->object);
    g_ui_map["shared_text"]="wrong UI text";
    g_story_texts["shared_text"]="translated story"; g_illust_get_conditions["shared_text"]="translated condition";
    assert(content_readback::inspect(&ds->object,"_story")["status"]=="failed");
    assert(string_table::patch(&storytable->object,g_story_texts,"TextData_UI","content",false,true)==1);
    assert(string_table::patch(&illusttable->object,g_illust_get_conditions,"TextData_UI","content",false,true)==1);
    auto sr=content_readback::inspect(&ds->object,"_story");
    auto ir=content_readback::inspect(&ds->object,"_illustGetConditionText");
    assert(sr["status"]=="passed" && sr["equal_fields"]==1 && !sr.contains("candidate_audit"));
    assert(ir["status"]=="passed" && ir["equal_fields"]==1 && !ir.contains("candidate_audit"));
    assert(value(storyrow,24)==u"translated story" && value(illustrow,24)==u"translated condition" && handles.empty());
    translation_evidence::active=saved_active;
    return 0;
}
