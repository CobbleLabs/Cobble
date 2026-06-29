// Property-test entry point for the pure navigation model (nav::).
//
// Task 2.1 stands up the target, harness, and generators only. The individual
// design properties (Properties 1-7) are added by tasks 2.2-2.8, each in its
// own translation unit that includes "generators.hpp". This file owns main(),
// configures the iteration count, and runs a single trivial smoke property to
// prove the target builds, links RapidCheck, and runs.
//
// Each property in this target runs a minimum of 100 iterations. RapidCheck's
// default is already 100; we set it explicitly via RC_PARAMS below so the
// guarantee does not depend on the default.

#include <cstdlib>

#include <rapidcheck.h>

#include "generators.hpp"
#include "ui/menu/menu_nav.hpp"

namespace {

// Ensure every property runs at least 100 successful iterations regardless of
// the RapidCheck default. Set before any rc::check call so the configuration is
// picked up when RapidCheck first reads RC_PARAMS.
void configureIterations() {
#if defined(_WIN32)
    _putenv_s("RC_PARAMS", "max_success=100");
#else
    setenv("RC_PARAMS", "max_success=100", /*overwrite=*/1);
#endif
}

} // namespace

int main() {
    configureIterations();

    bool allPassed = true;

    // Smoke property A: commit() is idempotent once pending == current. This is
    // a structural sanity check (NOT one of the named design properties) whose
    // only job is to prove the harness compiles and runs.
    allPassed &= rc::check(
        "smoke: commit is idempotent when pending equals current",
        [](int category, int entry) {
            nav::NavState s;
            s.category = s.nextCategory = category;
            s.entry = s.nextEntry = entry;
            s.view = s.nextView = nav::ViewKind::List;

            const nav::NavState once = nav::commit(s);
            const nav::NavState twice = nav::commit(once);

            RC_ASSERT(once.category == twice.category);
            RC_ASSERT(once.entry == twice.entry);
            RC_ASSERT(once.view == twice.view);
        });

    // Smoke property B: exercises the shared generators (CategoryInfo with
    // possible entryCount == 0, and EventArg with possibly-invalid indices) and
    // proves requestTransition/resolveView run over the generated input space.
    // The assertion is intentionally trivial (a ViewKind is always one of three
    // values) so this does not implement any of Properties 1-7.
    allPassed &= rc::check(
        "smoke: generators drive requestTransition to a defined view",
        [](const nav::CategoryInfo& info, const nav_test::EventArg& ea) {
            const nav::NavState start;
            const nav::NavState next =
                nav::requestTransition(start, ea.event, ea.arg, info);
            const nav::ViewKind view = nav::resolveView(nav::commit(next), info);

            RC_ASSERT(view == nav::ViewKind::List ||
                      view == nav::ViewKind::Config ||
                      view == nav::ViewKind::SpecialPage);
        });

    return allPassed ? 0 : 1;
}
