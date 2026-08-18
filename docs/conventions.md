# Coding conventions — mtg_cpp

These rules apply to every translation unit in this repository. They exist to
guarantee the code is memory-safe by construction, not just by luck. CI enforces
them with clang-tidy, ASan/UBSan, and a strict clang-format check.

## 1. Memory safety (hard rules)

1. **No raw owning pointers.** `new`/`delete`, `malloc`/`free`, and raw owning
   `T*` are forbidden in project code. Ownership is expressed with value types,
   `std::unique_ptr`, or `std::shared_ptr`. Raw pointers (non-owning observers)
   are allowed only where the lifetime is guaranteed by a parent object and
   documented.
2. **No manual resource management.** Sockets, threads, file handles, SFML
   resources, mutexes and condition variables are always wrapped in RAII types.
   Destructors do the cleanup; there is no `close()`-in-the-wrong-branch code.
3. **Bounds-checked access on untrusted data.** Use `std::array`, `std::span`,
   `.at()`, and iterators. Do not index external/network/JSON data with raw `[]`.
4. **Validate before you trust.** Every byte that crosses the network, every JSON
   record, and every parsed Arena line is schema-validated before it is stored or
   used. Malformed input is rejected with an error — never UB, never a crash.
5. **Single-owner threading model.** Game state is mutated only on the main
   thread. Background threads (network, art download) communicate exclusively
   through the bounded `MessageQueue`; they never touch game state directly.
6. **No integer overflow / truncation on untrusted values.** Size and count
   fields from the wire or from files are range-checked before arithmetic.
7. **Exceptions are fine, leaks are not.** RAII guarantees cleanup even when an
   exception unwinds. Catch at boundaries (main, worker-thread entry points) and
   convert to a clean error.

## 2. Language

- C++20. No C++17-and-earlier idioms unless forced by a dependency.
- Prefer value semantics; return by value; avoid output parameters.
- Use `std::optional` for "no result" (never sentinel values like `-1` or `""`
  to mean "missing" unless a dependency forces it).
- Use `std::span` for array parameter passing.
- `const` correctness everywhere; prefer `const` member functions and
  `constexpr` where possible.
- No macros except include guards and CMake-injected version strings.

## 3. Style

- Enforced by `.clang-format` (LLVM base, 100-column limit). Run
  `clang-format --dry-run --Werror src/*.cpp tests/*.cpp` before pushing.
- Names: `PascalCase` types, `camelBack` functions/variables, `kPascalCase`
  constants, `snake_case` namespaces, trailing `_` on private members.
- Headers: `#pragma once`; declare, then define in the `.cpp`. Keep headers
  dependency-light (forward declarations where possible).
- No comments that restate the code. Comments explain *why*.

## 4. Testing

- Every rule/helper is unit tested with GoogleTest; tests live beside the code
  under `tests/`.
- Tests must be deterministic and headless (no display, no network, no wall-clock
  sleeps except a bounded poll timeout).
- A failing test is a bug; a passing test is not proof. Cover the edge cases:
  empty inputs, maximum bounds, malformed data, no-op idempotence.

## 5. Verification (run before every milestone)

```bash
scripts/ci-linux.sh            # build + ctest + clang-format + clang-tidy (clang, ASan)
scripts/ci-linux.sh gcc        # same with GCC
scripts/ci-windows.ps1         # Windows MSVC Debug build + ctest
```

## 6. Sanctioned exceptions

- **SFML event union** — SFML models input events as `sf::Event`, a C union.
  Reading `event.key.code` (etc.) is the one allowed union access; the
  `cppcoreguidelines-pro-type-union-access` check is disabled for this reason.
  Project code never introduces its own unions.
- **`nlohmann::json`** — lookups on untrusted documents always use `.at()`
  (throws on missing/invalid keys), never `operator[]`. `operator[]` is reserved
  for building documents, where an inserted default is the intended behavior.
