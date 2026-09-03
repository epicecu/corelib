#!/usr/bin/env python3
"""Discover and assemble versioned VitePress documentation."""

from __future__ import annotations

import argparse
import json
import os
from dataclasses import dataclass
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from typing import Callable, Iterable, Sequence


STABLE_TAG = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
VERSIONED_DOCS_FILES = (
    "docs/.vitepress/config.mts",
    "package-lock.json",
    "tools/docs_versions.py",
)


@dataclass(frozen=True, order=True)
class Version:
    major: int
    minor: int
    patch: int
    tag: str


def parse_stable_tag(tag: str) -> Version | None:
    """Parse the repository's numeric stable SemVer tag convention."""
    match = STABLE_TAG.fullmatch(tag)
    if match is None:
        return None
    return Version(*(int(component) for component in match.groups()), tag=tag)


def select_latest_patch(tags: Iterable[str]) -> list[Version]:
    """Retain the highest patch version for each major/minor release line."""
    selected: dict[tuple[int, int], Version] = {}
    for tag in tags:
        version = parse_stable_tag(tag)
        if version is None:
            continue
        key = (version.major, version.minor)
        if key not in selected or version.patch > selected[key].patch:
            selected[key] = version
    return sorted(selected.values(), reverse=True)


def select_compatible_versions(
    tags: Iterable[str], compatible: Callable[[str], bool]
) -> list[Version]:
    """Select retained versions that contain the documentation toolchain."""
    return select_latest_patch(tag for tag in tags if compatible(tag))


def run(
    command: Sequence[str],
    *,
    cwd: Path,
    env: dict[str, str] | None = None,
    capture_output: bool = False,
) -> subprocess.CompletedProcess[str]:
    """Run one checked documentation command."""
    return subprocess.run(
        command,
        cwd=cwd,
        env=env,
        check=True,
        text=True,
        capture_output=capture_output,
    )


def repository_root() -> Path:
    """Return the current Git repository root."""
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        check=True,
        text=True,
        capture_output=True,
    )
    return Path(result.stdout.strip()).resolve()


def git_tags(root: Path) -> list[str]:
    """List repository tags."""
    result = run(["git", "tag", "--list"], cwd=root, capture_output=True)
    return [tag for tag in result.stdout.splitlines() if tag]


def tag_contains(root: Path, tag: str, path: str) -> bool:
    """Test whether a tag contains a required path."""
    result = subprocess.run(
        ["git", "cat-file", "-e", f"{tag}:{path}"],
        cwd=root,
        text=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.returncode == 0


def compatible_tag(root: Path, tag: str) -> bool:
    """Test whether a tag can build versioned documentation."""
    return all(tag_contains(root, tag, path) for path in VERSIONED_DOCS_FILES)


def discover_versions(root: Path) -> list[Version]:
    """Discover retained, documentation-capable release versions."""
    return select_compatible_versions(
        git_tags(root), lambda tag: compatible_tag(root, tag)
    )


def normalise_project_base(value: str) -> str:
    """Normalise a VitePress project base path."""
    base = f"/{value.strip('/')}" if value.strip("/") else ""
    return f"{base}/"


def normalise_site_origin(value: str) -> str:
    """Validate and normalise a documentation site origin."""
    origin = value.rstrip("/")
    if not origin.startswith(("https://", "http://")):
        raise ValueError("site origin must start with https:// or http://")
    return origin


def output_path(root: Path, value: str) -> Path:
    """Resolve and validate a documentation output below build/."""
    output = (
        (root / value).resolve()
        if not Path(value).is_absolute()
        else Path(value).resolve()
    )
    build_root = (root / "build").resolve()
    try:
        relative = output.relative_to(build_root)
    except ValueError as error:
        raise ValueError(
            "documentation output must be beneath the repository build directory"
        ) from error
    if relative == Path("."):
        raise ValueError("documentation output cannot be the complete build directory")
    return output


def version_entries(
    versions: Sequence[Version], site_origin: str, project_base: str
) -> list[dict[str, str]]:
    """Build the version-selector entries."""
    root_url = f"{site_origin}{project_base}"
    entries = [{"label": "Latest", "url": root_url}]
    entries.extend(
        {"label": version.tag, "url": f"{root_url}{version.tag}/"}
        for version in versions
    )
    return entries


def build_checkout(
    checkout: Path,
    output: Path,
    base: str,
    label: str,
    source_ref: str,
    entries: Sequence[dict[str, str]],
) -> None:
    """Build documentation for one checkout."""
    environment = os.environ.copy()
    environment.update(
        {
            "CORELIB_DOCS_VERSION": label,
            "CORELIB_DOCS_SOURCE_REF": source_ref,
            "CORELIB_DOCS_VERSIONS": json.dumps(entries, separators=(",", ":")),
        }
    )
    run(
        [
            "task",
            "docs:build",
            f"DOCS_BASE={base}",
            f"DOCS_OUT_DIR={output}",
        ],
        cwd=checkout,
        env=environment,
    )


def build_versioned_site(
    root: Path, output: Path, site_origin: str, project_base: str
) -> list[Version]:
    """Build Latest and all retained stable documentation revisions."""
    versions = discover_versions(root)
    entries = version_entries(versions, site_origin, project_base)

    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)

    build_checkout(root, output, project_base, "Latest", "main", entries)

    for version in versions:
        with tempfile.TemporaryDirectory(
            prefix=f"corelib-docs-{version.tag}-"
        ) as temporary:
            checkout = Path(temporary) / "source"
            run(
                ["git", "worktree", "add", "--detach", str(checkout), version.tag],
                cwd=root,
            )
            try:
                build_checkout(
                    checkout,
                    output / version.tag,
                    f"{project_base}{version.tag}/",
                    version.tag,
                    version.tag,
                    entries,
                )
            finally:
                run(
                    ["git", "worktree", "remove", "--force", str(checkout)],
                    cwd=root,
                )

    expected = [output / "index.html"]
    expected.extend(output / version.tag / "index.html" for version in versions)
    missing = [str(path) for path in expected if not path.is_file()]
    if missing:
        raise RuntimeError(f"documentation builds are missing: {', '.join(missing)}")

    (output / "versions.json").write_text(
        json.dumps(entries, indent=2) + "\n", encoding="utf-8"
    )
    return versions


def list_versions(root: Path) -> int:
    """Print deployable documentation revisions."""
    versions = discover_versions(root)
    if versions:
        print("Deployable documentation versions:")
        for version in versions:
            print(f"  {version.tag}")
    else:
        print("No compatible release tags; only Latest will be deployed.")
    return 0


def main() -> int:
    """Run the selected version-documentation command."""
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("list", help="List retained compatible release tags")
    build_parser = subparsers.add_parser("build", help="Build the versioned site")
    build_parser.add_argument("--output", default="build/docs/site")
    build_parser.add_argument(
        "--site-origin", default="https://epicecu.github.io"
    )
    build_parser.add_argument("--project-base", default="/corelib/")
    arguments = parser.parse_args()

    root = repository_root()
    if arguments.command == "list":
        return list_versions(root)

    output = output_path(root, arguments.output)
    site_origin = normalise_site_origin(arguments.site_origin)
    project_base = normalise_project_base(arguments.project_base)
    versions = build_versioned_site(root, output, site_origin, project_base)
    print(f"Built Latest and {len(versions)} retained release version(s) in {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
