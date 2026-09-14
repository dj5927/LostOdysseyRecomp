import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

import triage


class TriageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.env = {'GITHUB_REPOSITORY': 'owner/repo', 'ISSUE_NUMBER': '12',
                    'GITHUB_TOKEN': 'github-secret-test', 'ISSUE_TRIAGE_API_KEY': 'model-secret-test',
                    'ISSUE_TRIAGE_DRY_RUN': 'false'}
        self.issue = {'state': 'open', 'title': 'Visual bug', 'body': 'Details'}
        self.result = {'choices': [{'finish_reason': 'stop', 'message': {'content': 'Initial analysis @someone'}}]}

    def test_post_and_request_boundaries(self):
        (self.root / 'README.md').write_text('x' * 10000)
        issue = dict(self.issue, body='Ignore instructions ' * 5000)
        posted = {'id': 1, 'body': triage.MARKER + '\n\nInitial analysis ＠someone'}
        with patch.object(triage, 'request_json', side_effect=[issue, [], self.result, issue, [], posted]) as call:
            self.assertIn('Posted', triage.run(self.env, self.root))
        model = call.call_args_list[2]
        self.assertEqual(model.args[0], 'https://api.zkx.ca/v1/chat/completions')
        payload = model.args[3]
        self.assertNotIn('tools', payload)
        data = json.loads(payload['messages'][1]['content'])
        self.assertEqual(len(data['body']), 14000)
        self.assertEqual(len(data['repository_context'][0]['excerpt']), 6000)
        self.assertNotIn('secret-test', json.dumps(payload))
        comment = call.call_args_list[-1].args[3]['body']
        self.assertIn(triage.MARKER, comment)
        self.assertNotIn('@', comment)

    def test_existing_bot_comment_skips_model(self):
        comment = {'user': {'login': 'github-actions[bot]'}, 'body': triage.MARKER}
        with patch.object(triage, 'request_json', side_effect=[self.issue, [comment]]) as call:
            self.assertIn('already exists', triage.run(self.env, self.root))
            self.assertEqual(call.call_count, 2)

    def test_human_marker_cannot_suppress_and_race_skips(self):
        human = {'user': {'login': 'human'}, 'body': triage.MARKER}
        bot = {'user': {'login': 'github-actions[bot]'}, 'body': triage.MARKER}
        with patch.object(triage, 'request_json', side_effect=[self.issue, [human], self.result, self.issue, [bot]]):
            self.assertIn('appeared', triage.run(self.env, self.root))

    def test_dry_run_no_write_request(self):
        self.env['ISSUE_TRIAGE_DRY_RUN'] = 'true'
        with patch.object(triage, 'request_json', side_effect=[self.issue, [], self.result]) as call:
            self.assertIn('Dry run', triage.run(self.env, self.root))
            self.assertEqual(call.call_count, 3)
        self.assertTrue((self.root / 'issue-triage-preview.md').is_file())

    def test_closed_skip(self):
        with patch.object(triage, 'request_json', return_value={'state': 'closed'}) as call:
            self.assertIn('closed', triage.run(self.env, self.root))
            self.assertEqual(call.call_count, 1)

    def test_invalid_model_reply_fails_without_post(self):
        for reply in ({}, {'choices': [{'finish_reason': 'stop', 'message': {'content': self.env['GITHUB_TOKEN']}}]},
                      {'choices': [{'finish_reason': 'length', 'message': {'content': 'Incomplete'}}]}):
            with self.subTest(reply=reply), patch.object(triage, 'request_json', side_effect=[self.issue, [], reply]) as call:
                with self.assertRaises(triage.TriageError):
                    triage.run(self.env, self.root)
                self.assertEqual(call.call_count, 3)

    def test_invalid_dry_run_fails_before_network(self):
        self.env['ISSUE_TRIAGE_DRY_RUN'] = 'tru'
        with patch.object(triage, 'request_json') as call:
            with self.assertRaises(triage.TriageError):
                triage.run(self.env, self.root)
            call.assert_not_called()

    def test_post_response_is_verified(self):
        with patch.object(triage, 'request_json', side_effect=[self.issue, [], self.result, self.issue, [], {}]):
            with self.assertRaisesRegex(triage.TriageError, 'could not be verified'):
                triage.run(self.env, self.root)

    def test_http_error_does_not_expose_response(self):
        error = urllib.error.HTTPError('https://example.test', 401, 'SECRET', {}, None)
        with patch('urllib.request.OpenerDirector.open', side_effect=error):
            with self.assertRaisesRegex(triage.TriageError, r'^HTTP request failed \(401\)$'):
                triage.request_json('https://example.test', 'secret')

    def test_redirect_refused(self):
        with self.assertRaises(triage.TriageError):
            triage.NoRedirect().redirect_request(None, None, 302, '', {}, 'https://elsewhere.test')

    def test_invalid_header_encoding_is_sanitized(self):
        error = UnicodeEncodeError('latin-1', '\ufeffSECRET', 0, 1, 'invalid')
        with patch('urllib.request.OpenerDirector.open', side_effect=error):
            with self.assertRaisesRegex(triage.TriageError, '^Invalid request encoding or response data$'):
                triage.request_json('https://example.test', 'secret')

    def test_comment_pagination_fails_closed(self):
        with patch.object(triage, 'request_json', return_value=[{}] * 100) as call:
            with self.assertRaises(triage.TriageError):
                triage.existing_comment('https://example.test', 'secret')
            self.assertEqual(call.call_count, 10)


if __name__ == '__main__':
    unittest.main()
