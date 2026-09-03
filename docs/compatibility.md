# Compatibility and releases

| Corelib | Portable Frame Protocol | Transaction Protocol | C | Optional C++ facade |
| --- | --- | --- | --- | --- |
| 1.0.x | 1 | 2 | C11 | C++14 with ETL 20.x |

Corelib follows Semantic Versioning for its public source and package
interfaces. Protocol versions are reported separately by `corelib_version()`
and may evolve independently of the library version.

## Documentation revisions

The site root represents the current `main` branch and is labelled **Latest**.
Stable numeric tags are published below their own version paths. The newest
patch from every major/minor release line is retained, so a developer can read
documentation matching deployed firmware without keeping every superseded
patch online.

Versioned publishing begins with the first release containing the documentation
toolchain. Earlier tags are not reconstructed or backfilled.

## Wire compatibility

The change from the former expansion of PFP to **Portable Frame Protocol** is a
terminology change. The PFP acronym, protocol version, frame format, numeric
values, and `CORELIB_PFP_*` source identifiers are unchanged.
