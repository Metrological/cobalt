#!/usr/bin/env vpython3
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for scm.py."""

import logging
import os
import sys
import unittest

if sys.version_info.major == 2:
  import mock
else:
  from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from testing_support import fake_repos

import scm
import subprocess2


def callError(code=1, cmd='', cwd='', stdout=b'', stderr=b''):
  return subprocess2.CalledProcessError(code, cmd, cwd, stdout, stderr)


class GitWrapperTestCase(unittest.TestCase):
  def setUp(self):
    super(GitWrapperTestCase, self).setUp()
    self.root_dir = '/foo/bar'

  @mock.patch('scm.GIT.Capture')
  def testGetEmail(self, mockCapture):
    mockCapture.return_value = 'mini@me.com'
    self.assertEqual(scm.GIT.GetEmail(self.root_dir), 'mini@me.com')
    mockCapture.assert_called_with(['config', 'user.email'], cwd=self.root_dir)

  def testRefToRemoteRef(self):
    remote = 'origin'
    refs = {
        'refs/branch-heads/1234': ('refs/remotes/branch-heads/', '1234'),
        # local refs for upstream branch
        'refs/remotes/%s/foobar' % remote: ('refs/remotes/%s/' % remote,
                                            'foobar'),
        '%s/foobar' % remote: ('refs/remotes/%s/' % remote, 'foobar'),
        # upstream ref for branch
        'refs/heads/foobar': ('refs/remotes/%s/' % remote, 'foobar'),
        # could be either local or upstream ref, assumed to refer to
        # upstream, but probably don't want to encourage refs like this.
        'heads/foobar': ('refs/remotes/%s/' % remote, 'foobar'),
        # underspecified, probably intended to refer to a local branch
        'foobar': None,
        # tags and other refs
        'refs/tags/TAG': None,
        'refs/changes/34/1234': None,
    }
    for k, v in refs.items():
      r = scm.GIT.RefToRemoteRef(k, remote)
      self.assertEqual(r, v, msg='%s -> %s, expected %s' % (k, r, v))

  def testRemoteRefToRef(self):
    remote = 'origin'
    refs = {
        'refs/remotes/branch-heads/1234': 'refs/branch-heads/1234',
        # local refs for upstream branch
        'refs/remotes/origin/foobar': 'refs/heads/foobar',
        # tags and other refs
        'refs/tags/TAG': 'refs/tags/TAG',
        'refs/changes/34/1234': 'refs/changes/34/1234',
        # different remote
        'refs/remotes/other-remote/foobar': None,
        # underspecified, probably intended to refer to a local branch
        'heads/foobar': None,
        'origin/foobar': None,
        'foobar': None,
        None: None,
      }
    for k, v in refs.items():
      r = scm.GIT.RemoteRefToRef(k, remote)
      self.assertEqual(r, v, msg='%s -> %s, expected %s' % (k, r, v))


class RealGitTest(fake_repos.FakeReposTestBase):
  def setUp(self):
    super(RealGitTest, self).setUp()
    self.enabled = self.FAKE_REPOS.set_up_git()
    if self.enabled:
      self.cwd = scm.os.path.join(self.FAKE_REPOS.git_base, 'repo_1')
    else:
      self.skipTest('git fake repos not available')

  def testIsValidRevision(self):
    # Sha1's are [0-9a-z]{32}, so starting with a 'z' or 'r' should always fail.
    self.assertFalse(scm.GIT.IsValidRevision(cwd=self.cwd, rev='zebra'))
    self.assertFalse(scm.GIT.IsValidRevision(cwd=self.cwd, rev='r123456'))
    # Valid cases
    first_rev = self.githash('repo_1', 1)
    self.assertTrue(scm.GIT.IsValidRevision(cwd=self.cwd, rev=first_rev))
    self.assertTrue(scm.GIT.IsValidRevision(cwd=self.cwd, rev='HEAD'))

  def testIsAncestor(self):
    self.assertTrue(scm.GIT.IsAncestor(
        self.cwd, self.githash('repo_1', 1), self.githash('repo_1', 2)))
    self.assertFalse(scm.GIT.IsAncestor(
        self.cwd, self.githash('repo_1', 2), self.githash('repo_1', 1)))
    self.assertFalse(scm.GIT.IsAncestor(
        self.cwd, self.githash('repo_1', 1), 'zebra'))

  def testGetAllFiles(self):
    self.assertEqual(['DEPS','origin'], scm.GIT.GetAllFiles(self.cwd))

  def testGetSetConfig(self):
    key = 'scm.test-key'

    self.assertIsNone(scm.GIT.GetConfig(self.cwd, key))
    self.assertEqual(
        'default-value', scm.GIT.GetConfig(self.cwd, key, 'default-value'))

    scm.GIT.SetConfig(self.cwd, key, 'set-value')
    self.assertEqual('set-value', scm.GIT.GetConfig(self.cwd, key))
    self.assertEqual(
        'set-value', scm.GIT.GetConfig(self.cwd, key, 'default-value'))

    scm.GIT.SetConfig(self.cwd, key)
    self.assertIsNone(scm.GIT.GetConfig(self.cwd, key))
    self.assertEqual(
        'default-value', scm.GIT.GetConfig(self.cwd, key, 'default-value'))

  def testGetSetBranchConfig(self):
    branch = scm.GIT.GetBranch(self.cwd)
    key = 'scm.test-key'

    self.assertIsNone(scm.GIT.GetBranchConfig(self.cwd, branch, key))
    self.assertEqual(
        'default-value',
        scm.GIT.GetBranchConfig(self.cwd, branch, key, 'default-value'))

    scm.GIT.SetBranchConfig(self.cwd, branch, key, 'set-value')
    self.assertEqual(
        'set-value', scm.GIT.GetBranchConfig(self.cwd, branch, key))
    self.assertEqual(
        'set-value',
        scm.GIT.GetBranchConfig(self.cwd, branch, key, 'default-value'))
    self.assertEqual(
        'set-value',
        scm.GIT.GetConfig(self.cwd, 'branch.%s.%s' % (branch, key)))

    scm.GIT.SetBranchConfig(self.cwd, branch, key)
    self.assertIsNone(scm.GIT.GetBranchConfig(self.cwd, branch, key))

  def testFetchUpstreamTuple_NoUpstreamFound(self):
    self.assertEqual(
        (None, None), scm.GIT.FetchUpstreamTuple(self.cwd))

  @mock.patch('scm.GIT.GetRemoteBranches', return_value=['origin/master'])
  def testFetchUpstreamTuple_GuessOriginMaster(self, _mockGetRemoteBranches):
    self.assertEqual(
        ('origin', 'refs/heads/master'), scm.GIT.FetchUpstreamTuple(self.cwd))

  def testFetchUpstreamTuple_RietveldUpstreamConfig(self):
    scm.GIT.SetConfig(self.cwd, 'rietveld.upstream-branch', 'rietveld-upstream')
    scm.GIT.SetConfig(self.cwd, 'rietveld.upstream-remote', 'rietveld-remote')
    self.assertEqual(
        ('rietveld-remote', 'rietveld-upstream'),
        scm.GIT.FetchUpstreamTuple(self.cwd))
    scm.GIT.SetConfig(self.cwd, 'rietveld.upstream-branch')
    scm.GIT.SetConfig(self.cwd, 'rietveld.upstream-remote')

  @mock.patch('scm.GIT.GetBranch', side_effect=callError())
  def testFetchUpstreamTuple_NotOnBranch(self, _mockGetBranch):
    scm.GIT.SetConfig(self.cwd, 'rietveld.upstream-branch', 'rietveld-upstream')
    scm.GIT.SetConfig(self.cwd, 'rietveld.upstream-remote', 'rietveld-remote')
    self.assertEqual(
        ('rietveld-remote', 'rietveld-upstream'),
        scm.GIT.FetchUpstreamTuple(self.cwd))
    scm.GIT.SetConfig(self.cwd, 'rietveld.upstream-branch')
    scm.GIT.SetConfig(self.cwd, 'rietveld.upstream-remote')

  def testFetchUpstreamTuple_BranchConfig(self):
    branch = scm.GIT.GetBranch(self.cwd)
    scm.GIT.SetBranchConfig(self.cwd, branch, 'merge', 'branch-merge')
    scm.GIT.SetBranchConfig(self.cwd, branch, 'remote', 'branch-remote')
    self.assertEqual(
        ('branch-remote', 'branch-merge'), scm.GIT.FetchUpstreamTuple(self.cwd))
    scm.GIT.SetBranchConfig(self.cwd, branch, 'merge')
    scm.GIT.SetBranchConfig(self.cwd, branch, 'remote')

  def testFetchUpstreamTuple_AnotherBranchConfig(self):
    branch = 'scm-test-branch'
    scm.GIT.SetBranchConfig(self.cwd, branch, 'merge', 'other-merge')
    scm.GIT.SetBranchConfig(self.cwd, branch, 'remote', 'other-remote')
    self.assertEqual(
        ('other-remote', 'other-merge'),
        scm.GIT.FetchUpstreamTuple(self.cwd, branch))
    scm.GIT.SetBranchConfig(self.cwd, branch, 'merge')
    scm.GIT.SetBranchConfig(self.cwd, branch, 'remote')


if __name__ == '__main__':
  if '-v' in sys.argv:
    logging.basicConfig(level=logging.DEBUG)
  unittest.main()

# vim: ts=2:sw=2:tw=80:et:
