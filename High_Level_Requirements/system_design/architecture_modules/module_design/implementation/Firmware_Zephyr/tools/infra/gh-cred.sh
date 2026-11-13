#!/bin/bash
# gh-cred.sh - git credential helper that serves the LIVE GITHUB_REPO_TOKEN
# from the gateway environment at push time (survives token rotation:
# JP updates the compose env, pushes keep working - no URL-embedded secrets).
# Installed as repo-local credential.helper on the VPS canonical clone.
T=$(tr '\0' '\n' < /proc/1/environ | grep '^GITHUB_REPO_TOKEN=' | cut -d= -f2-)
echo "username=skixer2"
echo "password=$T"
