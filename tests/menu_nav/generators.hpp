#pragma once

// RapidCheck generators (Arbitrary specializations) for the pure navigation
// model under test (nav::CategoryInfo, nav::Event) plus a paired event/arg
// type used to drive arbitrary navigation sequences.
//
// These generators are the shared harness used by the per-property tests
// (tasks 2.2-2.8). They deliberately produce edge inputs called out in the
// design's Testing Strategy:
//   * CategoryInfo with entryCount == 0 (unresolved / empty categories, Req 2.5)
//   * a mix of special and module categories
//   * event arguments that are both valid AND invalid (negative or out of
//     range) so the clamping / no-op paths in requestTransition are exercised.
//
// Header-only so every property test file can include it directly.

#include <rapidcheck.h>

#include "ui/menu/menu_nav.hpp"

namespace nav_test {

// A navigation event paired with the integer argument requestTransition()
// consumes: a category index for SelectCategory, an entry index for
// SelectEntry, and ignored for Back. Args are intentionally allowed to be out
// of range so invalid-index handling is covered.
struct EventArg {
    nav::Event event = nav::Event::Back;
    int        arg   = 0;
};

// Upper bound (exclusive) used when generating category/entry indices so that
// generated sequences stay in a small, meaningful space while still producing
// out-of-range values (negatives and values past this bound are generated too).
inline constexpr int kMaxIndex = 13;

} // namespace nav_test

namespace rc {

// Arbitrary nav::CategoryInfo: any mix of special / module categories, with an
// entry count spanning the empty case (0) up to a small upper bound.
template <>
struct Arbitrary<nav::CategoryInfo> {
    static Gen<nav::CategoryInfo> arbitrary() {
        return gen::build<nav::CategoryInfo>(
            gen::set(&nav::CategoryInfo::isSpecial, gen::arbitrary<bool>()),
            // [0, kMaxIndex): 0 covers the unresolved / empty category case.
            gen::set(&nav::CategoryInfo::entryCount,
                     gen::inRange(0, nav_test::kMaxIndex)));
    }
};

// Arbitrary nav::Event: one of the three navigation events.
template <>
struct Arbitrary<nav::Event> {
    static Gen<nav::Event> arbitrary() {
        return gen::element(nav::Event::SelectCategory,
                            nav::Event::SelectEntry,
                            nav::Event::Back);
    }
};

// Arbitrary EventArg: any event paired with an index that may be valid or
// invalid. The range spans negatives and values beyond kMaxIndex so the
// clamping and no-op branches are reached.
template <>
struct Arbitrary<nav_test::EventArg> {
    static Gen<nav_test::EventArg> arbitrary() {
        return gen::build<nav_test::EventArg>(
            gen::set(&nav_test::EventArg::event, gen::arbitrary<nav::Event>()),
            gen::set(&nav_test::EventArg::arg,
                     gen::inRange(-3, nav_test::kMaxIndex + 3)));
    }
};

} // namespace rc
