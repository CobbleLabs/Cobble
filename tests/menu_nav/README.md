# menu_nav property tests

Property-based tests for the pure navigation model in
`src/ui/menu/menu_nav.hpp` (`nav::` namespace). The model is ImGui-free, so it
is compiled and exercised here in complete isolation from the Cobble DLL.

This is a **standalone MSBuild console target** (`menu_nav_tests.vcxproj`) added
to `Cobble.sln`. It does not build, link, or depend on the Cobble DLL, and it
does not touch the main `Cobble.vcxproj`.

## Dependency: RapidCheck

The tests use [RapidCheck](https://github.com/emil-e/rapidcheck) as the
property-testing library. RapidCheck is vendored as a git submodule under
`infra/ext/rapidcheck` and compiled directly into the test target (the project
globs `infra/ext/rapidcheck/src/**/*.cpp`), so there is no separate library
build or install step.

If the submodule is not present (fresh clone without `--recurse-submodules`, or
the directory is empty), fetch it with:

```powershell
# From the repo root. Adds the submodule and records it in .gitmodules.
git submodule add https://github.com/emil-e/rapidcheck infra/ext/rapidcheck

# Or, if it is already registered, just populate it:
git submodule update --init infra/ext/rapidcheck
```

No further wiring is required — the include path
(`infra/ext/rapidcheck/include`) and the globbed sources are already configured
in `menu_nav_tests.vcxproj`.

## Building and running

Build just the test project with MSBuild:

```powershell
& "<path-to>\MSBuild.exe" tests\menu_nav\menu_nav_tests.vcxproj /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
```

(`build.ps1` builds the Cobble DLL and loader; the test target is intentionally
separate and is built directly as above.)

Run the produced executable:

```powershell
.\out\tests\menu_nav\MenuNavTests.exe
```

The process exits `0` when all properties pass and non-zero otherwise.

## Iteration count

Every property runs a **minimum of 100 iterations**. `main.cpp` sets
`RC_PARAMS=max_success=100` before any check runs, so the guarantee does not
rely on the RapidCheck default. To run more iterations ad hoc, set the env var
before launching, e.g. `\$env:RC_PARAMS = "max_success=1000"`.

## Layout

| File | Purpose |
|---|---|
| `menu_nav_tests.vcxproj` | Standalone MSBuild console target; compiles `menu_nav.hpp` + RapidCheck. |
| `main.cpp` | `main()`, iteration config, and a trivial smoke property. |
| `generators.hpp` | RapidCheck `Arbitrary` generators for `nav::CategoryInfo`, `nav::Event`, and the paired `EventArg` (valid + invalid indices, `entryCount == 0`). |

## Adding the property tests (tasks 2.2-2.8)

Each design property (Properties 1-7) is implemented in its own `.cpp` file in
this directory. Add the file, include `"generators.hpp"`, write a single
`rc::check(...)` with the property's tag comment, then add it to the
`<ItemGroup>` of `menu_nav_tests.vcxproj` next to `main.cpp`.
