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

    def test_reports_duplicate_word_names(self) -> None:
        src = ": foo 1 ;\n: foo 2 ;\n"
        result = analyze(src)
        messages = [d.message for d in result.diagnostics]
        self.assertTrue(any("Word name 'foo' is already defined" in m for m in messages))

    def test_reports_invalid_local_names(self) -> None:
        src = ": foo ( a a dup foo bad:name ) a ;\n"
        result = analyze(src)
        messages = [d.message for d in result.diagnostics]
        self.assertTrue(any("Local name 'a' is already defined" in m for m in messages))
        self.assertTrue(any("Local name 'dup' is reserved" in m for m in messages))
        self.assertTrue(any("Local name 'foo' conflicts with word name" in m for m in messages))
        self.assertTrue(any("Local name 'bad:name' cannot contain ':'" in m for m in messages))

    def test_reports_reserved_word_name(self) -> None:
        src = ": dup 1 ;\n"
        result = analyze(src)
        messages = [d.message for d in result.diagnostics]
        self.assertTrue(any("Word name 'dup' is reserved" in m for m in messages))

    def test_reports_nested_word_definition(self) -> None:
        src = ": outer 1 : inner 2 ; ;\n"
        result = analyze(src)
        messages = [d.message for d in result.diagnostics]
        self.assertTrue(any("Nested word definition not allowed" in m for m in messages))

    def test_reports_control_marker_mismatches(self) -> None:
        src = ": main then else do ;\n"
        result = analyze(src)
        messages = [d.message for d in result.diagnostics]
        self.assertTrue(any("Unexpected 'then'" in m for m in messages))
        self.assertTrue(any("Unexpected 'else'" in m for m in messages))
        self.assertTrue(any("Unexpected 'do'" in m for m in messages))

    def test_reports_missing_then_and_do(self) -> None:
        src = ": main if 1 end while 1 end ;\n"
        result = analyze(src)
        messages = [d.message for d in result.diagnostics]
        self.assertTrue(any("Missing 'then' for 'if' block" in m for m in messages))
        self.assertTrue(any("Missing 'do' for 'while' block" in m for m in messages))

    def test_reports_unexpected_signature_tokens_in_expression(self) -> None:
        src = ": main ( a ) a ;\n"
        result = analyze(src)
        messages = [d.message for d in result.diagnostics]
        self.assertTrue(any("main word cannot declare arguments" in m for m in messages))

    def test_reports_alias_cannot_declare_locals(self) -> None:
        src = ":: bad ( a ) a ;\n: main 1 bad ;\n"
        result = analyze(src)
        messages = [d.message for d in result.diagnostics]
        self.assertTrue(
            any("Alias 'bad' cannot declare arguments or local variables" in m for m in messages)
        )

    def test_reports_reserved_char_in_word_name(self) -> None:
        src = ": foo|bar 1 ;\n: main foo|bar ;\n"
        result = analyze(src)
        messages = [d.message for d in result.diagnostics]
        self.assertTrue(any("Word name 'foo|bar' cannot contain '|'" in m for m in messages))


if __name__ == "__main__":
    unittest.main()
