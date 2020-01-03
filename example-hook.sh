#!/usr/bin/env bash

case $1 in
  lease)
    echo $@ >> /tmp/hook.log
    ;;
  release)
    echo $@ >> /tmp/hook.log
    ;;
  endlearning)
    echo "$@ - learning phase is over" >> /tmp/hook.log
    ;;
esac
