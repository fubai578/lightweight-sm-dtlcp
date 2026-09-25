#!/bin/bash
# push_to_github.sh — 将本仓库创建为 GitHub 私有仓库并推送
#
# 用法（令牌通过环境变量传递，不落盘、不进 shell history）：
#   GH_TOKEN=<你的PAT> bash scripts/push_to_github.sh [仓库名]
#
# 令牌要求：
#   classic token   → 勾选 repo 作用域
#   fine-grained    → Contents: Read and write + Administration: Read and write
set -e
cd "$(dirname "$0")/.."
REPO="${1:-lightweight-sm-dtlcp}"

if [ -z "$GH_TOKEN" ]; then
  echo "错误：未提供 GH_TOKEN。用法：GH_TOKEN=<你的PAT> bash scripts/push_to_github.sh [仓库名]"
  exit 1
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "错误：未找到 gh CLI（https://cli.github.com/）"
  exit 1
fi

echo "== 用 GH_TOKEN 校验身份 =="
gh api user -q .login

echo "== 创建私有仓库 $REPO 并推送 main =="
gh repo create "$REPO" --private --source=. --push

echo "== 完成 =="
gh repo view "$REPO" --json url -q .url
