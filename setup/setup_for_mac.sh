#!/bin/bash

# 현재 실행된 스크립트의 절대 경로
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GITROOT_DIR="$SCRIPT_DIR/../"
DEVENV_DIR="$GITROOT_DIR/devenv"

# 에러 발생 시 스크립트 중단
# set -e

#/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install git
brew install ansible

# input sudo password!!
ansible-playbook -K -e "devenv_dir=${DEVENV_DIR}" -i "ansible_inventory" "ansible_provision.yml"
