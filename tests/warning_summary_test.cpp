#include "warning_summary.hpp"
#include <cassert>
#include <cstdio>

using namespace warning_summary;
int main() {
    State state;
    Batch batch;
    for (int i = 0; i < 10000; ++i) batch["missing_local"].add("id-" + std::to_string(i));
    assert(batch.at("missing_local").count == 10000 && batch.at("missing_local").examples.size() == 3);
    auto first = state.submit("table", batch, 0);
    assert(first.size() == 1 && first[0].group.count == 10000 && first[0].suppressed == 0);
    for (int i = 1; i <= 1000; ++i) assert(state.submit("table", batch, i).empty());
    assert(state.flush(60000).empty()); // Unchanged retries never produce periodic noise.
    auto changed = batch;
    changed["missing_local"].add("a-new-id-outside-original-samples");
    auto update = state.submit("table", changed, 60001);
    assert(update.size() == 1 && update[0].suppressed == 1000 && update[0].group.count == 10001);
    changed["missing_local"].add("another-id");
    assert(state.submit("table", changed, 60002).empty());
    assert(state.flush(90000).empty());
    auto pending = state.flush(90001); // Timer publishes even if patching has stopped.
    assert(pending.size() == 1 && pending[0].group.count == 10002 && pending[0].suppressed == 1);
    auto recovery = state.submit("table", {}, 90002);
    assert(recovery.size() == 1 && recovery[0].recovered && state.size() == 0);
    assert(state.submit("table", batch, 90003).size() == 1); // Recurrence is visible immediately.
    assert(state.submit("other_table", batch, 90004).size() == 1);
    assert(state.submit("other_table", {}, 90005, false).empty());
    assert(state.size() == 2); // An incomplete traversal cannot claim a previous issue recovered.

    State revert;
    assert(revert.submit("table", batch, 0).size() == 1);
    assert(revert.submit("table", changed, 1).empty());
    assert(revert.submit("table", batch, 2).empty());
    assert(revert.flush(30000).empty()); // Do not publish a superseded intermediate state.

    Group forward, reverse;
    forward.add("alpha"); forward.add("beta");
    reverse.add("beta"); reverse.add("alpha");
    assert(forward == reverse && forward.examples == reverse.examples);
    Group same_count_new_id = forward;
    same_count_new_id = {}; same_count_new_id.add("alpha"); same_count_new_id.add("gamma");
    assert(!(same_count_new_id == forward));

    State bounded;
    Batch one; one["reason"].add("sample");
    for (size_t i = 0; i < State::capacity; ++i) assert(bounded.submit(std::to_string(i), one, 0).size() == 1);
    auto overflow = bounded.submit("overflow", one, 1);
    assert(overflow.size() == 1 && overflow[0].reason == "group_limit");
    assert(bounded.submit("overflow_again", one, 2).empty());
    assert(bounded.size() == State::capacity && bounded.dropped_groups == 2);
    bounded.reset();
    assert(bounded.size() == 0 && bounded.dropped_groups == 0);
    assert(bounded.submit("overflow", one, 3).size() == 1);

    assert(sample("one\ntwo\tthree\r") == "one two three ");
    assert(sample(std::string(159, 'a') + "\xe4\xb8\xad").size() == 159);
    std::puts("warning_summary: bounded samples, retry suppression, timed flush, recovery, reset and overflow passed");
}
