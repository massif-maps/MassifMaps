"""python3 -m unittest discover -s scripts/tests"""
import importlib.util
import json
import os
import subprocess
import tempfile
import unittest

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'release-version.py')
spec = importlib.util.spec_from_file_location('release_version', SCRIPT)
rv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rv)


class Repo(unittest.TestCase):
    def setUp(self):
        self.cwd = os.getcwd()
        self.dir = tempfile.TemporaryDirectory()
        os.chdir(self.dir.name)
        self.compat = os.path.join(self.dir.name, '.compat.json')
        rv.COMPAT = self.compat
        self.min_sdk('6.0.0')
        self.git('init', '-q', '-b', 'master')
        self.git('config', 'user.name', 't')
        self.git('config', 'user.email', 't@t')
        self.git('config', 'commit.gpgsign', 'false')
        self.git('config', 'tag.gpgsign', 'false')
        self.n = 0
        self.commit('chore: init', 'all/native/init.cpp')
        for tag in ('v6.0.0', 'massif-styles-v1.0.0', 'style-tools-v6.0.0'):
            self.git('tag', tag)

    def tearDown(self):
        os.chdir(self.cwd)
        self.dir.cleanup()

    def min_sdk(self, value):
        with open(self.compat, 'w') as f:
            json.dump({'minSdk': value}, f)

    def git(self, *args):
        subprocess.run(['git', *args], check=True, capture_output=True)

    def commit(self, message, *paths):
        self.n += 1
        for path in paths:
            os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
            with open(path, 'a') as f:
                f.write(f'{self.n}\n')
        self.git('add', '-A')
        self.git('commit', '-q', '-m', message)

    def plan(self, product, bump, prerelease=False, sdk=''):
        return rv.plan(product, bump, prerelease, 'HEAD', sdk)


class Auto(Repo):
    def test_nothing_since_the_tag_releases_nothing(self):
        out = self.plan('sdk', 'auto')
        self.assertEqual((out['version'], out['suggested'], out['commits']), ('', 'none', 0))

    def test_fix_is_a_patch_feat_a_minor_breaking_a_major(self):
        self.commit('fix(vt): a', 'all/native/a.cpp')
        self.assertEqual(self.plan('sdk', 'auto')['version'], '6.0.1')
        self.commit('feat(layers): b', 'all/native/b.cpp')
        self.assertEqual(self.plan('sdk', 'auto')['version'], '6.1.0')
        self.commit('feat(layers)!: c', 'all/native/c.cpp')
        self.assertEqual(self.plan('sdk', 'auto')['version'], '7.0.0')

    def test_breaking_footer_is_a_major(self):
        self.commit('fix(api): a\n\nBREAKING CHANGE: gone', 'all/native/a.cpp')
        self.assertEqual(self.plan('sdk', 'auto')['version'], '7.0.0')

    def test_a_product_only_counts_its_own_paths(self):
        self.commit('feat(styles): a', 'styles/massif/a.json')
        self.assertEqual(self.plan('sdk', 'auto')['commits'], 0)
        self.assertEqual(self.plan('styles', 'auto')['version'], '1.1.0')
        self.assertEqual(self.plan('tools', 'auto')['commits'], 0)

    def test_the_cartocss_library_belongs_to_the_sdk_and_the_tools(self):
        self.commit('fix(cartocss): a', 'libs-massif/cartocss/a.cpp')
        self.assertEqual(self.plan('sdk', 'auto')['version'], '6.0.1')
        self.assertEqual(self.plan('tools', 'auto')['version'], '6.0.1')
        self.assertEqual(self.plan('styles', 'auto')['version'], '')

    def test_the_cli_belongs_to_the_tools_alone(self):
        self.commit('feat(style-tools): a', 'tools/style-cli/src/a.ts')
        self.assertEqual(self.plan('tools', 'auto')['version'], '6.1.0')
        self.assertEqual(self.plan('sdk', 'auto')['commits'], 0)

    def test_docs_alone_release_nothing(self):
        self.commit('fix(docs): a', 'docs/a.md')
        for product in ('sdk', 'styles', 'tools'):
            self.assertEqual(self.plan(product, 'auto')['commits'], 0)

    def test_a_product_stays_at_its_own_base(self):
        self.commit('fix(styles): a', 'styles/massif/a.json')
        self.git('tag', 'massif-styles-v1.0.1')
        self.assertEqual(self.plan('styles', 'auto')['commits'], 0)
        self.assertEqual(self.plan('styles', 'patch')['version'], '1.0.2')


class Bump(Repo):
    def test_none_skips_even_with_commits(self):
        self.commit('feat(vt): a', 'all/native/a.cpp')
        self.assertEqual(self.plan('sdk', 'none')['version'], '')

    def test_patch_minor_major_ignore_the_commits(self):
        self.assertEqual(self.plan('styles', 'patch')['tag'], 'massif-styles-v1.0.1')
        self.assertEqual(self.plan('styles', 'minor')['tag'], 'massif-styles-v1.1.0')
        self.assertEqual(self.plan('styles', 'major')['tag'], 'massif-styles-v2.0.0')

    def test_an_explicit_version(self):
        self.assertEqual(self.plan('tools', '6.3.0')['tag'], 'style-tools-v6.3.0')
        self.assertEqual(self.plan('tools', '6.3.0-rc.7')['version'], '6.3.0-rc.7')

    def test_bad_input(self):
        with self.assertRaises(SystemExit):
            self.plan('sdk', 'bigger')

    def test_an_existing_or_older_tag_is_refused(self):
        self.git('tag', 'v6.0.1')
        with self.assertRaises(SystemExit):
            self.plan('sdk', '6.0.1')
        with self.assertRaises(SystemExit):
            self.plan('sdk', '5.9.0')

    def test_a_product_without_a_final_tag_is_refused(self):
        self.git('tag', '-d', 'style-tools-v6.0.0')
        with self.assertRaises(SystemExit):
            self.plan('tools', 'patch')


class Prerelease(Repo):
    def test_the_candidate_number_counts_the_tags(self):
        self.assertEqual(self.plan('sdk', 'minor', True)['version'], '6.1.0-rc.1')
        self.git('tag', 'v6.1.0-rc.1')
        self.assertEqual(self.plan('sdk', 'minor', True)['version'], '6.1.0-rc.2')
        self.git('tag', 'v6.1.0-rc.2')
        self.assertEqual(self.plan('sdk', 'minor')['version'], '6.1.0')

    def test_a_candidate_is_not_a_base(self):
        self.git('tag', 'v6.1.0-rc.1')
        out = self.plan('sdk', 'minor')
        self.assertEqual((out['base'], out['version']), ('v6.0.0', '6.1.0'))
        with self.assertRaises(SystemExit):
            self.plan('sdk', 'patch')

    def test_an_explicit_final_version_becomes_a_candidate(self):
        self.assertEqual(self.plan('styles', '2.0.0', True)['version'], '2.0.0-rc.1')


class MinSdk(Repo):
    def test_a_released_sdk_needs_no_sdk_in_the_run(self):
        self.assertEqual(self.plan('styles', 'patch')['min_sdk'], '6.0.0')

    def test_next_is_the_sdk_released_with_the_styles(self):
        self.min_sdk('next')
        self.assertEqual(self.plan('styles', 'patch', sdk='6.1.0')['min_sdk'], '6.1.0')
        self.assertEqual(self.plan('styles', 'patch', True, sdk='6.1.0-rc.2')['min_sdk'], '6.1.0')
        with self.assertRaises(SystemExit):
            self.plan('styles', 'patch')

    def test_a_number_above_the_latest_sdk_needs_the_sdk_in_the_run(self):
        self.min_sdk('6.1.0')
        with self.assertRaises(SystemExit):
            self.plan('styles', 'patch')
        with self.assertRaises(SystemExit):
            self.plan('styles', 'patch', sdk='6.0.1')
        self.assertEqual(self.plan('styles', 'patch', sdk='6.2.0')['min_sdk'], '6.1.0')

    def test_a_skipped_release_has_none(self):
        self.assertNotIn('min_sdk', self.plan('styles', 'none'))


if __name__ == '__main__':
    unittest.main()
