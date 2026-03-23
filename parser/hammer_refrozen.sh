#!/bin/bash
set -eu

run_case() {
    local profile="$1"
    local expr="$2"

    echo "expr    : $expr"
    echo "profile : $profile"
    ./stream_passes --profile "$profile" --model stream "$expr"
    ./stream_passes --profile "$profile" --model frozen "$expr"
    ./stream_passes --profile "$profile" --model boundary "$expr"
    echo
}

run_case mux213 '\\% capacity'
run_case mux213 '[switch(1,1,{\\% capacity})]'
run_case mux213 '[if(1,{\\% capacity})]'
run_case mux213 '[case(1,1,{\\% capacity})]'
run_case mux213 '[iter(a b,{\\% capacity})]'
run_case mux213 '[switch(1,1,\\% capacity)]'
run_case mux213 '[switch(1,1,{\\%b})]'
run_case mux213 '[iter(a b,{\\%b})]'
run_case penn '[iter(a b,{\\% capacity})]'
