import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("ai_keys", Path(__file__).with_name("ai-keys.py"))
keys = importlib.util.module_from_spec(spec)
spec.loader.exec_module(keys)


class Migration(unittest.TestCase):
    def test_coexisting_keys_and_repeat(self):
        files = {"ai-translate.txt": b"aiDeepseekKey=test-chat\naiBailianKey=test-asr\naiProvider=1\n"}
        _, updated, config, added = keys.migrate(files.get)
        self.assertEqual(config, {"DEEPSEEK_API_KEY": "test-chat", "DASHSCOPE_API_KEY": "test-asr"})
        self.assertEqual(len(added), 2)
        files["ai-keys.txt"] = updated
        self.assertEqual(keys.migrate(files.get)[1], updated)

    def test_existing_shared_not_overwritten(self):
        files={"ai-keys.txt": b"# keep this\nDEEPSEEK_API_KEY=authoritative\nUNKNOWN=keep\n",
               "ai-translate.txt": b"aiDeepseekKey=old\naiBailianKey=asr\n"}
        _,updated,config,_ = keys.migrate(files.get)
        self.assertEqual(config["DEEPSEEK_API_KEY"], "authoritative")
        self.assertIn(b"UNKNOWN=keep",updated)
        self.assertIn(b"# keep this",updated)

    def test_conflict_no_write_or_secret_in_error(self):
        files={"ai-translate.txt": b"aiDeepseekKey=secret-one\n", "voice-ai.txt": b"voiceChatKey=secret-two\n"}
        with self.assertRaises(ValueError) as caught:
            keys.migrate(files.get)
        self.assertNotIn("secret",str(caught.exception))
        self.assertIn("voice-ai.txt",str(caught.exception))

    def test_voice_bailian_chat_not_misclassified(self):
        files={"voice-ai.txt": b"voiceChatEndpoint=https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions\nvoiceChatKey=bailian-only\nvoiceAsrKey=bailian-only\n"}
        config=keys.migrate(files.get)[2]
        self.assertEqual(config.get("DASHSCOPE_API_KEY"),"bailian-only")
        self.assertNotIn("DEEPSEEK_API_KEY",config)

    def test_custom_does_not_become_deepseek(self):
        files={"ai-game-workshop/config.json": b'{"key":"custom","endpoint":"https://custom.example"}'}
        self.assertFalse(keys.migrate(files.get)[2].get("DEEPSEEK_API_KEY"))


if __name__ == "__main__":
    unittest.main()
