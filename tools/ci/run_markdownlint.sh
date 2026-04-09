#!/usr/bin/env bash
set -euo pipefail

markdownlint-cli2 README.md 'docs/**/*.md'
