import json
import sys
from pathlib import Path
import tempfile
import unittest
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from docs_versions import (  # noqa: E402
    Version,
    build_versioned_site,
    normalise_project_base,
    normalise_site_origin,
    output_path,
    parse_stable_tag,
    select_compatible_versions,
    select_latest_patch,
    version_entries,
)


class DocumentationVersionsTest(unittest.TestCase):
    def test_parses_only_numeric_stable_semver_tags(self) -> None:
        self.assertEqual(parse_stable_tag("1.2.3"), Version(1, 2, 3, "1.2.3"))
        for tag in ("v1.2.3", "1.2", "1.2.3-beta.1", "01.2.3", "update1"):
            self.assertIsNone(parse_stable_tag(tag))

    def test_retains_latest_patch_for_each_minor_line(self) -> None:
        selected = select_latest_patch(
            ["1.2.4", "1.2.5", "1.3.0", "2.0.0", "2.0.1", "update1"]
        )
        self.assertEqual(
            [version.tag for version in selected], ["2.0.1", "1.3.0", "1.2.5"]
        )

    def test_sorts_versions_numerically(self) -> None:
        selected = select_latest_patch(["1.9.9", "1.10.0", "10.0.0", "2.0.0"])
        self.assertEqual(
            [version.tag for version in selected],
            ["10.0.0", "2.0.0", "1.10.0", "1.9.9"],
        )

    def test_excludes_versions_without_the_documentation_toolchain(self) -> None:
        compatible = select_compatible_versions(
            ["0.3.0", "1.0.0", "1.0.1"], lambda tag: tag == "1.0.0"
        )
        self.assertEqual([version.tag for version in compatible], ["1.0.0"])

    def test_builds_latest_and_release_urls(self) -> None:
        entries = version_entries(
            [Version(1, 2, 5, "1.2.5")],
            "https://epicecu.github.io",
            normalise_project_base("corelib"),
        )
        self.assertEqual(
            entries,
            [
                {"label": "Latest", "url": "https://epicecu.github.io/corelib/"},
                {
                    "label": "1.2.5",
                    "url": "https://epicecu.github.io/corelib/1.2.5/",
                },
            ],
        )

    def test_validates_origins_and_output_paths(self) -> None:
        self.assertEqual(normalise_site_origin("https://example.com/"), "https://example.com")
        with self.assertRaises(ValueError):
            normalise_site_origin("example.com")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assertEqual(output_path(root, "build/docs/site"), root / "build/docs/site")
            for unsafe in ("build", "docs/site"):
                with self.assertRaises(ValueError):
                    output_path(root, unsafe)

    def test_assembles_latest_and_each_selected_version(self) -> None:
        versions = [Version(2, 0, 1, "2.0.1"), Version(1, 2, 5, "1.2.5")]
        builds: list[tuple[str, str, str]] = []

        def fake_build(
            checkout: Path,
            output: Path,
            base: str,
            label: str,
            source_ref: str,
            entries: object,
        ) -> None:
            del checkout, entries
            output.mkdir(parents=True, exist_ok=True)
            (output / "index.html").write_text(label, encoding="utf-8")
            builds.append((base, label, source_ref))

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "build" / "docs" / "site"
            stale = output / "1.2.4" / "index.html"
            stale.parent.mkdir(parents=True)
            stale.write_text("stale", encoding="utf-8")
            with (
                mock.patch("docs_versions.discover_versions", return_value=versions),
                mock.patch("docs_versions.build_checkout", side_effect=fake_build),
                mock.patch("docs_versions.run"),
            ):
                selected = build_versioned_site(
                    root, output, "https://epicecu.github.io", "/corelib/"
                )

            self.assertEqual(selected, versions)
            self.assertFalse(stale.exists())
            self.assertEqual(
                builds,
                [
                    ("/corelib/", "Latest", "main"),
                    ("/corelib/2.0.1/", "2.0.1", "2.0.1"),
                    ("/corelib/1.2.5/", "1.2.5", "1.2.5"),
                ],
            )
            manifest = json.loads((output / "versions.json").read_text())
            self.assertEqual(
                [entry["label"] for entry in manifest],
                ["Latest", "2.0.1", "1.2.5"],
            )


if __name__ == "__main__":
    unittest.main()
