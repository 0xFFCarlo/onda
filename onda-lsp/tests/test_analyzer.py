import unittest

from onda_lsp.analyzer import analyze


class AnalyzerTests(unittest.TestCase):
    def test_extracts_symbols_from_colon_forms(self) -> None:
        src = ": main 1 ;\n:fib dup ;\n"
        result = analyze(src)
        names = [s.name for s in result.symbols]
        self.assertIn("main", names)
        self.assertIn("fib", names)

    def test_reports_unclosed_block(self) -> None:
        src = ": main if 1 then 2 ;\n"
        result = analyze(src)
        messages = [d.message for d in result.diagnostics]
        self.assertTrue(any("Unclosed block" in m for m in messages))

    def test_reports_missing_semicolon(self) -> None:
        src = ": main 1 2\n"
        result = analyze(src)
        messages = [d.message for d in result.diagnostics]
        self.assertTrue(any("missing terminating ';'" in m for m in messages))

    def test_ignores_hash_inside_string_comment_strip(self) -> None:
        src = ': main "abc#def" .s ; # trailing comment\n'
        result = analyze(src)
        self.assertEqual(len(result.diagnostics), 0)


if __name__ == "__main__":
    unittest.main()
