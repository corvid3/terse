#!/bin/bash

if [ ! -d $HOME/.hewg/bootstrap/crow.terse ]; then
  mkdir -p $HOME/.hewg/bootstrap/crow.terse
fi

cp include/terse.hh $HOME/.hewg/bootstrap/crow.terse/

