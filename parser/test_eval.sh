#!/bin/bash
# Quick test harness for the evaluator.
# Each test is: input_expression expected_output

PASS=0
FAIL=0

check() {
    local input="$1"
    local expected="$2"
    local flags="${3:-}"
    local actual
    actual=$(printf '%s\n' "$input" | ./eval $flags 2>&1)
    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
        echo "FAIL: $input"
        echo "  expected: $expected"
        echo "  actual:   $actual"
    fi
}

# Arithmetic
check '[add(1,2)]' '3'
check '[add(1,mul(2,3))]' '7'
check '[sub(10,3)]' '7'
check '[mul(4,5)]' '20'
check '[div(10,3)]' '3'
check '[mod(10,3)]' '1'
check '[abs(-42)]' '42'
check '[inc(5)]' '6'
check '[dec(5)]' '4'
check '[power(2,10)]' '1024'
check '[sqrt(144)]' '12'
check '[max(3,7,2,9,1)]' '9'
check '[min(3,7,2,9,1)]' '1'
check '[floor(3.7)]' '3'
check '[ceil(3.2)]' '4'

# Comparison
check '[eq(5,5)]' '1'
check '[eq(5,3)]' '0'
check '[neq(5,3)]' '1'
check '[gt(5,3)]' '1'
check '[lt(3,5)]' '1'
check '[gte(5,5)]' '1'
check '[lte(5,5)]' '1'

# Boolean
check '[and(1,1,1)]' '1'
check '[and(1,0,1)]' '0'
check '[or(0,0,1)]' '1'
check '[not(0)]' '1'
check '[not(1)]' '0'
check '[xor(1,0)]' '1'
check '[t(0)]' '0'
check '[t(1)]' '1'

# Control flow
check '[if(eq(1,1),yes,no)]' 'yes'
check '[if(eq(1,2),yes,no)]' 'no'
check '[switch(b,a,alpha,b,beta,*,other)]' 'beta'
check '[switch(z,a,alpha,b,beta,*,other)]' 'other'

# String functions
check '[strlen(hello)]' '5'
check '[cat(hello,world)]' 'hello world'
check '[strcat(hello,world)]' 'helloworld'
check '[ucstr(hello)]' 'HELLO'
check '[lcstr(HELLO)]' 'hello'
check '[capstr(hello world)]' 'Hello world'
check '[left(hello,3)]' 'hel'
check '[right(hello,3)]' 'llo'
check '[mid(hello world,6,5)]' 'world'
check '[repeat(*,5)]' '*****'
check '[trim(  hello  )]' 'hello'

# List functions
check '[words(a b c d e)]' '5'
check '[first(a b c)]' 'a'
check '[rest(a b c)]' 'b c'
check '[last(a b c)]' 'c'
check '[sort(3 1 4 1 5 9 2 6)]' '1 1 2 3 4 5 6 9'
check '[lnum(5)]' '0 1 2 3 4'
check '[member(a b c d,c)]' '3'

# Set operations
check '[setunion(a b c,b c d)]' 'a b c d'
check '[setdiff(a b c,b c d)]' 'a'
check '[setinter(a b c,b c d)]' 'b c'

# Registers
check '[setq(0,hello)]%q0' 'hello'
check '[setr(0,hello)]' 'hello'

# Substitutions
check '%0 and %1' 'hello and world'
check '%%' '%'

# Escapes
check '\[not a bracket\]' '[not a bracket]'
check '\% capacity' '% capacity'
check '\\% capacity' '\ capacity'
check '\\% capacity' '% capacity' '--profile mux213'
check '% happy' '% happy' '--profile penn'

# Nested evaluation
check '[add(1,add(2,add(3,4)))]' '10'
check '[if(eq(add(1,2),3),match,no match)]' 'match'

# Iter with %i0 (proper FN_NOEVAL — body is evaluated per item)
check '[iter(a b c,%i0)]' 'a b c'
check '[iter(a b c,[strcat(%i0,-,[itext(0)])])]' 'a-a b-b c-c'

# Iter with nested function calls in body (the FN_NOEVAL proof)
check '[iter(10 20 30,[add(%i0,1)])]' '11 21 31'
check '[iter(hello world,[strlen(%i0)])]' '5 5'
check '[iter(3 1 4,[mul(%i0,2)])]' '6 2 8'

# Nested iter
check '[iter(a b,[iter(1 2,[strcat(%i1,%i0)])])]' 'a1 a2 b1 b2'

# Short-circuit boolean (FN_NOEVAL)
check '[cand(1,1,1)]' '1'
check '[cand(1,0,1)]' '0'
check '[cor(0,0,1)]' '1'
check '[cor(0,0,0)]' '0'

# if/switch with deferred evaluation (only evaluates chosen branch)
check '[if(1,[add(1,2)],[add(3,4)])]' '3'
check '[if(0,[add(1,2)],[add(3,4)])]' '7'
check '[switch(b,a,[add(1,1)],b,[add(2,2)],*,[add(3,3)])]' '4'
check '[case(hello,world,no,hello,yes)]' 'yes'

# lit() — returns unevaluated text
check '[lit([add(1,2)])]' '[add(1,2)]'

# @@() — discards without evaluating
check '[@@(this is a comment)]' ''

# Eval brackets force function checking
check '[add(1,2)]' '3'

# Brace groups with default flags (EV_EVAL set but not EV_STRIP_CURLY)
# should pass through as raw braces
check '{hello world}' '{hello world}'

# Nested iter with function calls in body
check '[iter(1 2 3,[mul(%i0,%i0)])]' '1 4 9'

# Complex expression: register + iter + function
check '[setq(0,prefix-)][iter(a b c,[strcat(%q0,%i0)])]' 'prefix-a prefix-b prefix-c'

echo ""
echo "Results: $PASS passed, $FAIL failed"
exit $FAIL
