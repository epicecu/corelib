# MISRA C analysis

The `quality:misra` task applies the open-source Cppcheck MISRA C:2012 addon,
including its Amendment 1 and Amendment 2 checks, to Corelib's owned C
implementation. It is a blocking regression gate, not a formal MISRA
compliance claim. Cppcheck provides partial automated coverage; project-level
compliance also requires the licensed guidelines, manual review, tool
justification, and a compliance process.

## Scope

The gate analyses every C implementation directly under `src/corelib` and
`src/protocol`, together with their owned C headers, under Cppcheck's 32-bit
and 64-bit Unix data models. The C++ facade, generated protocol sources,
vendored Nanopb, examples, tests, and hardware integrations are outside this
analysis scope.

The repository does not distribute proprietary MISRA rule text. Diagnostics
therefore use stable rule identifiers. A developer with authorised rule text
may use it locally with Cppcheck without committing it to this repository.

## Accepted findings

The reviewed deviation register in
`config/cppcheck/misra-suppressions.txt` contains these bounded categories:

- Public API declarations and definitions retain external linkage even though
  a standalone analysis cannot observe downstream consumers.
- The caller-owned storage API requires reviewed conversions between opaque,
  byte-aligned, and internal fixed-storage representations.
- Protocol parsing and serialisation expressions follow standard C precedence;
  the surrounding bounds checks and conformance vectors verify their meaning.
- Guard clauses keep invalid inputs and failed operations away from state
  changes and callbacks.
- Cppcheck 2.13 cannot resolve Corelib's internal opaque-structure typedefs in
  several `sizeof` and `alignof` expressions. These `misra-config` diagnostics
  are suppressed for the two affected implementation files; newer Cppcheck
  versions resolve the declarations correctly.

Generated and vendored paths are excluded wholesale because their sources are
maintained by their respective generators or upstream projects.

Any additional suppression requires a bounded scope and rationale in this
register. Unknown or newly reported rule identifiers fail the task.

The remediation baseline has no accepted findings for mixed essential types,
composite casts, excessive shift widths, multiple side effects, missing
compound-statement braces, indirect recursion, ignored return values, mutable
input parameters, discarded qualifiers, or pointer arithmetic. Reintroducing
any of those categories fails the blocking gate.
