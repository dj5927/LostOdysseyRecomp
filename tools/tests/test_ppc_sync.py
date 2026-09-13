"""Offline sync decisions and fast-forward main push races; no native build or network."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("ppc_sync", Path(__file__).resolve().parents[1] / "release/ppc_sync.py")
sync = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sync)
SHA = "a" * 40
OTHER_SHA = "b" * 40


class PpcSyncTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "build"
        self.build.mkdir()
        (self.build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Release\n")
        self.evidence = {"fingerprint": {"inputs": {"source": "abc"}}, "contract": {"FLAGS": "/MT /O2"}}
        self.environment = patch.dict(os.environ, {}, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)
        self.fp = patch.object(sync.ppc_prebuilt, "fingerprint", side_effect=lambda root: copy.deepcopy(self.evidence["fingerprint"])).start()
        self.contract = patch.object(sync.ppc_prebuilt, "compile_contract", side_effect=lambda *args: (copy.deepcopy(self.evidence["contract"]), "22.1")).start()
        self.addCleanup(patch.stopall)
        self.key, _ = sync.identity(self.root, self.build)
        self.manifest = {"schema": 1, **copy.deepcopy(self.evidence),
                         "library": {"name": sync.ppc_prebuilt.LIBRARY, "size": 10, "sha256": "c" * 64},
                         "chunks": [{"name": "ppc-0000.bin", "size": 10, "sha256": "d" * 64}]}

    def test_key_tracks_source_and_compile_contract(self):
        self.assertEqual(self.key, sync.identity(self.root, self.build)[0])
        self.evidence["fingerprint"]["inputs"]["source"] = "changed"
        source_key = sync.identity(self.root, self.build)[0]
        self.assertNotEqual(self.key, source_key)
        self.evidence["contract"]["FLAGS"] = "/MT /O1"
        self.assertNotEqual(source_key, sync.identity(self.root, self.build)[0])

    def test_key_has_no_network_build_or_config_side_effect(self):
        with patch.object(sync, "run") as run:
            sync.identity(self.root, self.build)
            run.assert_not_called()

    def test_commands_disable_interactive_authentication(self):
        with patch.object(sync.subprocess, "run") as run:
            sync.run(["git", "status"])
            self.assertEqual("0", run.call_args.kwargs["env"]["GIT_TERMINAL_PROMPT"])
            self.assertEqual("1", run.call_args.kwargs["env"]["GH_PROMPT_DISABLED"])

    def test_ci_and_recursive_sync_skip_even_force(self):
        for variable in ("CI", "GITHUB_ACTIONS", "LO_PPC_SYNC_ACTIVE"):
            with self.subTest(variable=variable), patch.dict(os.environ, {variable: "1"}), patch.object(sync, "run") as run:
                self.assertIsNone(sync.sync(self.root, self.build, force=True))
                run.assert_not_called()

    def test_disabled_sync_skips(self):
        with patch.object(sync, "enabled", return_value=False), patch.object(sync, "release_build") as build:
            self.assertIsNone(sync.sync(self.root, self.build))
            build.assert_not_called()

    def test_enabled_uses_git_boolean_parser(self):
        with patch.object(sync, "run", return_value=subprocess.CompletedProcess([], 0, "true\n", "")) as run:
            self.assertTrue(sync.enabled(self.root))
            self.assertIn("--bool", run.call_args.args[0])

    def test_imported_caller_skips_even_force(self):
        with (self.build / "CMakeCache.txt").open("a") as cache:
            cache.write("LO_PREBUILT_PPC_DIR:PATH=D:/prebuilt\n")
        with patch.object(sync, "remote_commit") as remote:
            self.assertIsNone(sync.sync(self.root, self.build, already_built=True, force=True))
            remote.assert_not_called()

    def test_freeze_built_library_does_not_build(self):
        library = self.build / "LostOdysseyRecompLib" / sync.ppc_prebuilt.LIBRARY
        library.parent.mkdir()
        library.write_bytes(b"synthetic existing archive")
        with patch.object(sync.ppc_prebuilt.subprocess, "run") as run:
            sync.ppc_prebuilt.write_bundle_from_built(self.root, self.build, self.root / "bundle")
            run.assert_not_called()
        manifest = sync.ppc_prebuilt.load_manifest(self.root / "bundle")
        self.assertEqual(library.stat().st_size, manifest["library"]["size"])

    def test_remote_missing_only_for_exit_two(self):
        with patch.object(sync, "run", return_value=subprocess.CompletedProcess([], 2, "", "")):
            self.assertIsNone(sync.remote_commit())
        for error in ("Authentication failed", "Could not resolve host"):
            with self.subTest(error=error), patch.object(sync, "run", return_value=subprocess.CompletedProcess([], 128, "", error)):
                with self.assertRaises(subprocess.CalledProcessError):
                    sync.remote_commit()

    def test_remote_ref_response_is_exact(self):
        result = subprocess.CompletedProcess([], 0, f"{SHA}\trefs/heads/main\n", "")
        with patch.object(sync, "run", return_value=result):
            self.assertEqual(SHA, sync.remote_commit())

    def test_unchanged_does_not_build_or_upload(self):
        with patch.object(sync, "remote_commit", return_value=SHA), patch.object(sync, "remote_manifest", return_value=self.manifest), patch.object(sync, "publish") as publish, patch.object(sync, "run") as run:
            self.assertEqual((self.key, SHA), sync.sync(self.root, self.build, force=True))
            publish.assert_not_called()
            run.assert_not_called()
        receipt = json.loads((self.root / "out/ppc-sync/receipt.json").read_text())
        self.assertEqual(SHA, receipt["commit"])
        self.assertNotIn("LO_PPC_SYNC_ACTIVE", os.environ)

    def test_old_main_identity_publishes_new_cache(self):
        bad = copy.deepcopy(self.manifest)
        bad["contract"]["FLAGS"] = "different"
        with patch.object(sync, "remote_commit", return_value=SHA), patch.object(sync, "remote_manifest", side_effect=[bad, self.manifest]), patch.object(sync, "publish", return_value=(OTHER_SHA, self.manifest)) as publish:
            self.assertEqual((self.key, OTHER_SHA), sync.sync(self.root, self.build, force=True))
            publish.assert_called_once()

    def test_matching_identity_invalid_metadata_fails(self):
        bad = copy.deepcopy(self.manifest)
        bad["chunks"][0]["sha256"] = "invalid"
        with patch.object(sync, "remote_commit", return_value=SHA), patch.object(sync, "remote_manifest", return_value=bad), patch.object(sync, "publish") as publish:
            with self.assertRaisesRegex(ValueError, "digest"):
                sync.sync(self.root, self.build, force=True)
            publish.assert_not_called()

    def test_main_can_advance_after_resolution(self):
        with patch.object(sync, "remote_commit", side_effect=[SHA, OTHER_SHA]) as remote, patch.object(sync, "remote_manifest", return_value=self.manifest):
            self.assertEqual((self.key, SHA), sync.sync(self.root, self.build, force=True))
            self.assertEqual(1, remote.call_count)

    def test_deleted_ref_is_not_hidden_by_local_receipt(self):
        sync.receipt(self.root, self.key, SHA)
        with patch.object(sync, "remote_commit", side_effect=[None, OTHER_SHA]), patch.object(sync, "remote_manifest", return_value=self.manifest), patch.object(sync, "publish", return_value=(OTHER_SHA, self.manifest)) as publish:
            sync.sync(self.root, self.build, already_built=True, force=True)
            publish.assert_called_once_with(self.root, self.build, self.key, self.evidence, True)

    def test_failed_upload_readback_is_not_success(self):
        different = copy.deepcopy(self.manifest)
        different["library"]["sha256"] = "e" * 64
        with patch.object(sync, "remote_commit", side_effect=[None, SHA]), patch.object(sync, "remote_manifest", return_value=different), patch.object(sync, "publish", return_value=(SHA, self.manifest)):
            with self.assertRaisesRegex(ValueError, "readback metadata mismatch"):
                sync.sync(self.root, self.build, force=True)
        self.assertFalse((self.root / "out/ppc-sync/receipt.json").exists())

    def test_non_release_configures_isolated_source_build(self):
        (self.build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Debug\n")
        with patch.object(sync, "run") as run:
            self.assertEqual(self.root / "out/build/ppc-sync-Release", sync.release_build(self.root, self.build))
            command = run.call_args.args[0]
            self.assertIn("-DLO_PPC_AUTO_SYNC=OFF", command)
            self.assertIn("-DLO_BUILD_RUNTIME=OFF", command)
            self.assertIn("-DLO_PREBUILT_PPC_DIR=", command)
            self.assertNotIn("--build", command)


class PpcSyncGitTests(unittest.TestCase):
    setUp = PpcSyncTests.setUp

    def prepare(self):
        # Actual local Git integration, retaining PATH/SystemRoot for Windows Git.
        self.environment.stop()
        self.remote = self.root / "remote.git"
        self.seed = self.root / "seed"
        self.git("init", "--bare", str(self.remote))
        self.git("init", "-b", "main", str(self.seed))
        self.git("config", "user.name", "Test", cwd=self.seed)
        self.git("config", "user.email", "test@example.invalid", cwd=self.seed)
        (self.seed / "default.xex").write_bytes(b"original xex")
        (self.seed / ".gitattributes").write_bytes(b"default.xex binary\nfeedback/** -text\n")
        (self.seed / "feedback").mkdir()
        (self.seed / "feedback/evidence.json").write_text('{"keep": true}')
        (self.seed / "ppc").mkdir()
        (self.seed / "ppc/old.bin").write_bytes(b"old bundle")
        self.git("add", ".", cwd=self.seed)
        self.git("commit", "-m", "initial", cwd=self.seed)
        self.git("remote", "add", "origin", str(self.remote), cwd=self.seed)
        self.git("push", "origin", "main", cwd=self.seed)
        self.original = self.git("rev-parse", "HEAD", cwd=self.seed)
        patch.object(sync, "REMOTE", str(self.remote)).start()
        patch.object(sync, "GIT_AUTH", ["git"]).start()
        self.writer = patch.object(sync.ppc_prebuilt, "write_bundle_from_built", side_effect=self.write_bundle).start()
        self.export = patch.object(sync.ppc_prebuilt, "export").start()

    def git(self, *args, cwd=None):
        return subprocess.run(["git", *args], cwd=cwd, check=True, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.strip()

    def write_bundle(self, root, build, output):
        output.mkdir()
        (output / "manifest.json").write_text(json.dumps(self.manifest))
        (output / "ppc-0000.bin").write_bytes(b"0123456789")

    def publish(self):
        return sync.publish(self.root, self.build, self.key, self.evidence, True)

    def assert_preserved(self, commit):
        for name in ("default.xex", ".gitattributes", "feedback/evidence.json"):
            self.assertEqual(self.git("rev-parse", f"{self.original}:{name}", cwd=self.seed),
                             self.git("--git-dir", str(self.remote), "rev-parse", f"{commit}:{name}"))
        refs = self.git("--git-dir", str(self.remote), "for-each-ref", "--format=%(refname)")
        self.assertEqual("refs/heads/main", refs)
        self.assertEqual([], list((self.root / "out/ppc-sync").glob("upload-*")))

    def test_publish_preserves_other_files_and_creates_only_main(self):
        self.prepare()
        commit, manifest = self.publish()
        self.assertEqual(commit, sync.remote_commit())
        self.assertEqual(self.manifest, manifest)
        self.assert_preserved(commit)
        self.assertEqual(self.original, self.git("--git-dir", str(self.remote), "rev-parse", f"{commit}^"))
        self.assertNotIn("ppc/old.bin", self.git("--git-dir", str(self.remote), "ls-tree", "-r", "--name-only", commit))
        self.writer.assert_called_once()
        self.export.assert_not_called()

    def test_matching_main_is_noop(self):
        self.prepare()
        first, _ = self.publish()
        second, _ = self.publish()
        self.assertEqual(first, second)
        self.assert_preserved(second)

    def test_concurrent_feedback_commit_retried_without_loss(self):
        self.prepare()
        original_run = sync.run
        pushes = []
        concurrent = []
        def race(command, **kwargs):
            if command[1:2] == ["push"]:
                pushes.append(command)
                if len(pushes) == 1:
                    (self.seed / "feedback/new.json").write_text("new feedback")
                    self.git("add", ".", cwd=self.seed)
                    self.git("commit", "-m", "concurrent feedback", cwd=self.seed)
                    self.git("push", "origin", "main", cwd=self.seed)
                    concurrent.append(self.git("rev-parse", "HEAD", cwd=self.seed))
            return original_run(command, **kwargs)
        with patch.object(sync, "run", side_effect=race):
            commit, _ = self.publish()
        self.assertEqual(2, len(pushes))
        self.assertEqual(concurrent[0], self.git("--git-dir", str(self.remote), "rev-parse", f"{commit}^"))
        self.assertEqual("new feedback", self.git("--git-dir", str(self.remote), "show", f"{commit}:feedback/new.json"))
        self.assert_preserved(commit)
        self.writer.assert_called_once()

    def test_continuous_main_advances_have_bounded_retries(self):
        self.prepare()
        original_run = sync.run
        pushes = []
        def race(command, **kwargs):
            if command[1:2] == ["push"]:
                pushes.append(command)
                (self.seed / "feedback/new.json").write_text(str(len(pushes)))
                self.git("add", ".", cwd=self.seed)
                self.git("commit", "-m", "concurrent feedback", cwd=self.seed)
                self.git("push", "origin", "main", cwd=self.seed)
            return original_run(command, **kwargs)
        with patch.object(sync, "run", side_effect=race):
            with self.assertRaises(subprocess.CalledProcessError):
                self.publish()
        self.assertEqual(4, len(pushes))
        self.assert_preserved(sync.remote_commit())
        self.writer.assert_called_once()

    def test_identity_change_does_not_push(self):
        self.prepare()
        with patch.object(sync, "identity", return_value=("changed", self.evidence)):
            with self.assertRaisesRegex(ValueError, "changed"):
                self.publish()
        self.assertEqual(self.original, sync.remote_commit())

    def test_unchanged_main_push_rejection_is_not_retried(self):
        self.prepare()
        original_run = sync.run
        pushes = []
        def reject(command, **kwargs):
            if command[1:2] == ["push"]:
                pushes.append(command)
                return subprocess.CompletedProcess(command, 1, "", "policy rejected")
            return original_run(command, **kwargs)
        with patch.object(sync, "run", side_effect=reject):
            with self.assertRaises(subprocess.CalledProcessError):
                self.publish()
        self.assertEqual(1, len(pushes))
        self.assertEqual(self.original, sync.remote_commit())


if __name__ == "__main__":
    unittest.main()
