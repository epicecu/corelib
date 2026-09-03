# MISRA C analysis

The `quality:misra` task applies the open-source Cppcheck MISRA C:2012 addon,
including its Amendment 1 and Amendment 2 checks, to Corelib's owned C
implementation. It is a blocking regression gate, not a formal MISRA
compliance claim. Cppcheck provides partial automated coverage; project-level
compliance also requires the licensed guidelines, manual review, tool
justification, and a compliance process.

## Scope

The gate analyses `src/corelib/device.c`, `src/corelib/gateway.c`,
`src/protocol/pfp.c`, and `src/protocol/transaction.c`, together with their
owned C headers, under Cppcheck's 32-bit and 64-bit Unix data models. The C++
facade, generated protocol sources, vendored Nanopb, examples, tests, and
hardware integrations are outside this analysis scope.

The repository does not distribute proprietary MISRA rule text. Diagnostics
therefore use stable rule identifiers. A developer with authorised rule text
may use it locally with Cppcheck without committing it to this repository.

## Accepted findings

The initial regression baseline in
`config/cppcheck/misra-suppressions.txt` records these bounded categories:

- Public API declarations and definitions retain external linkage even though
  a standalone analysis cannot observe downstream consumers.
- The caller-owned storage API requires reviewed conversions between opaque,
  byte-aligned, and internal fixed-storage representations.
- Fixed-width protocol parsing and serialisation use deliberate casts,
  arithmetic, shifts, precedence, and pointer traversal with bounds checked by
  the surrounding codec.
- Guard clauses and compact conditional bodies keep validation and error paths
  local; converting the established implementation solely for advisory
  control-flow findings would add churn without changing behaviour.
- Gateway route traversal is bounded by configured capacities and cycle checks.
- Selected return values are intentionally consumed only for their bounded
  side effects, and selected parameters act as local iterators.
- A small set of Cppcheck advisory findings is retained where const or scope
  changes would not alter the current safety boundary.

Generated and vendored paths are excluded wholesale because their sources are
maintained by their respective generators or upstream projects.

Any additional suppression requires a bounded scope and rationale in this
register. Unknown or newly reported rule identifiers fail the task.
