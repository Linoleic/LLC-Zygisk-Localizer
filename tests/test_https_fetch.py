"""Exercise the shipped Java transport against a private TLS server, without internet."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(all(shutil.which(x) for x in ('java', 'javac', 'keytool')), 'JDK required')
class HttpsFetchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='llc-https-')
        cls.directory = Path(cls.temp.name)
        cls.store = cls.directory / 'fixture.p12'
        subprocess.run(['keytool', '-genkeypair', '-alias', 'fixture', '-keyalg', 'RSA',
                        '-keystore', str(cls.store), '-storetype', 'PKCS12',
                        '-storepass', 'fixture-password', '-keypass', 'fixture-password',
                        '-dname', 'CN=localhost', '-ext', 'SAN=dns:localhost', '-validity', '2'],
                       check=True, capture_output=True)
        subprocess.run(['javac', '--release', '8', '-d', str(cls.directory),
                        str(ROOT / 'manager/https/HttpsFetch.java')], check=True, capture_output=True)
        subprocess.run(['javac', '-d', str(cls.directory), str(ROOT / 'tests/https/HttpsFixture.java')],
                       check=True, capture_output=True)
        cls.server = subprocess.Popen(['java', '-cp', str(cls.directory), 'HttpsFixture', str(cls.store)],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        cls.port = int(cls.server.stdout.readline())

    @classmethod
    def tearDownClass(cls):
        cls.server.terminate()
        cls.server.communicate(timeout=10)
        cls.temp.cleanup()

    def fetch(self, path='/ok', limit=1024, redirects=True, trusted=True, seconds=5, proxy=None):
        env = os.environ.copy()
        for key in ('HTTPS_PROXY', 'https_proxy', 'ALL_PROXY', 'all_proxy'):
            env.pop(key, None)
        if proxy:
            env['HTTPS_PROXY'] = proxy
        args = ['java']
        if trusted:
            args += ['-Djavax.net.ssl.trustStore=' + str(self.store),
                     '-Djavax.net.ssl.trustStorePassword=fixture-password']
        args += ['-cp', str(self.directory), 'llc.HttpsFetch', f'https://localhost:{self.port}{path}',
                 str(limit), str(seconds), '1' if redirects else '0']
        return subprocess.run(args, env=env, capture_output=True, timeout=15)

    def test_verified_tls_stream_and_https_redirect(self):
        for path in ('/ok', '/redirect', '/chunked'):
            with self.subTest(path=path):
                result = self.fetch(path)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, b'fixture-body\nLLC_HTTP_STATUS:200')

    def test_untrusted_certificate_is_rejected(self):
        self.assertEqual(self.fetch(trusted=False).returncode, 60)

    def test_declared_and_streamed_size_limits(self):
        for path in ('/ok', '/chunked'):
            self.assertEqual(self.fetch(path, limit=4).returncode, 63)

    def test_redirect_policy(self):
        self.assertEqual(self.fetch('/redirect', redirects=False).returncode, 47)
        self.assertEqual(self.fetch('/insecure').returncode, 47)
        self.assertEqual(self.fetch('/loop').returncode, 47)

    def test_http_errors_keep_status_without_error_body(self):
        for path, status in (('/forbidden', 403), ('/limited', 429), ('/missing', 404), ('/error', 503)):
            with self.subTest(status=status):
                result = self.fetch(path)
                self.assertEqual(result.returncode, 22)
                self.assertEqual(result.stdout, f'\nLLC_HTTP_STATUS:{status}'.encode())

    def test_http_errors_timeout_and_proxy_configuration(self):
        self.assertEqual(self.fetch('/error').returncode, 22)
        self.assertEqual(self.fetch('/slow', seconds=1).returncode, 28)
        self.assertEqual(self.fetch(proxy='socks5://127.0.0.1:9').returncode, 3)


if __name__ == '__main__':
    unittest.main()
