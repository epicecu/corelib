import sys
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from docs_api import (  # noqa: E402
    ApiEntry,
    check_pages,
    clean_space,
    member_entry,
    public_members,
    write_pages,
)


class DocumentationApiTest(unittest.TestCase):
    def test_collapses_source_whitespace(self) -> None:
        self.assertEqual(clean_space(" one\n  two\tthree "), "one two three")

    def test_extracts_public_function_signature_and_docs(self) -> None:
        root = ET.fromstring(
            """
            <doxygen><memberdef kind="function" prot="public">
              <definition>corelib_status_t corelib_tick</definition>
              <argsstring>(corelib_context_t *context, uint64_t now)</argsstring>
              <name>corelib_tick</name>
              <briefdescription><para>Advances queued work.</para></briefdescription>
              <detaileddescription><para>Uses monotonic time.</para></detaileddescription>
            </memberdef></doxygen>
            """
        )
        entries = public_members(root, {"function"})
        self.assertEqual(
            entries,
            [
                ApiEntry(
                    "corelib_tick",
                    "corelib_status_t corelib_tick(corelib_context_t *context, uint64_t now)",
                    "Advances queued work.\n\nUses monotonic time.",
                )
            ],
        )

    def test_extracts_enum_values(self) -> None:
        node = ET.fromstring(
            """
            <memberdef kind="enum" prot="public">
              <name>corelib_status_t</name>
              <enumvalue><name>CORELIB_OK</name></enumvalue>
              <enumvalue><name>CORELIB_BUSY</name></enumvalue>
              <briefdescription><para>Operation status.</para></briefdescription>
            </memberdef>
            """
        )
        entry = member_entry(node)
        self.assertEqual(entry.signature, "typedef enum { ... } corelib_status_t")
        self.assertIn("`CORELIB_OK`, `CORELIB_BUSY`", entry.docs)

    def test_writes_and_checks_deterministic_pages(self) -> None:
        pages = {"c/index.md": "generated\n", "cpp/index.md": "generated\n"}
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            write_pages(output, pages)
            check_pages(output, pages)
            (output / "c/index.md").write_text("stale\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "out of date"):
                check_pages(output, pages)


if __name__ == "__main__":
    unittest.main()
