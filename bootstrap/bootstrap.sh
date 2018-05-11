#!/usr/bin/env sh
set -e

REVISION=$1

start() {
    ansible-playbook -e core_revision=$REVISION run.yml
}

init() {
    ansible-playbook -e core_revision=$REVISION init.yml
}

case "$2" in
    "init")
        init
        ;;
    "start")
        start
        ;;
    *)
        start
esac
