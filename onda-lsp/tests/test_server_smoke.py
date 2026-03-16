import unittest

from onda_lsp.server import OndaLspServer


class ServerSmokeTests(unittest.TestCase):
    def test_initialize_has_basic_capabilities(self) -> None:
        server = OndaLspServer()
        result = server._on_initialize()
        caps = result["capabilities"]
        self.assertTrue(caps["hoverProvider"])
        self.assertTrue(caps["documentSymbolProvider"])

    def test_hover_includes_stdlib_function_docs(self) -> None:
        server = OndaLspServer()
        uri = "file:///tmp/test.onda"
        server.documents.open(uri=uri, text=": main \"x\" strlen ;\n", version=1)
        result = server._on_hover(
            {
                "textDocument": {"uri": uri},
                "position": {"line": 0, "character": 11},
            }
        )
        self.assertIsNotNone(result)
        if result is None:
            return
        value = result["contents"]["value"]
        self.assertIn("strlen", value)
        self.assertIn("Stack:", value)
        self.assertIn("( str -- len )", value)
        self.assertIn("C-string length", value)


if __name__ == "__main__":
    unittest.main()
