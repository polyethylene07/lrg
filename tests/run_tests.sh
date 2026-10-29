#!/bin/sh
# make test 调的就是这一个文件：造输入、跑用例、报结果。
# 输入落在 tests/data/，make clean 会连二进制一起删掉。
set -u
cd "$(dirname "$0")/.." || exit 1

LRG=${LRG:-./lrg}
D=tests/data         # 到时候创建这样一个文件夹
tmp=$(mktemp) || exit 1
trap 'rm -f "$tmp"' EXIT
fail=0

[ -f "$LRG" ] || { echo "找不到 $LRG,先 make"; exit 1; }

mkdir -p "$D"
printf 'boot ok\ndisk full at 03:00\nERROR: retry later\nboot ok again\nerror: give up\n' > "$D/sample.log"
printf 'error handling rules\nall good\n' > "$D/notes.txt"
printf 'a\n\nb\n' > "$D/blank.txt"
printf 'abc\ndef' > "$D/nonl.txt"
{ head -c 1048575 /dev/zero | tr '\0' 'a'; printf 'needle\n'; } > "$D/long.txt"
{ head -c 70000 /dev/zero | tr '\0' 'a'; printf 'needle\n'; } > "$D/cross.txt"
yes 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abc \
    | head -n 200000 > "$D/big.txt"

# t 名字 期望退出码 期望输出 命令...
t() {
    name=$1; want_st=$2; want=$3; shift 3
    "$@" >"$tmp" 2>/dev/null; st=$?
    got=$(cat "$tmp")
    if [ "$st" -eq "$want_st" ] && [ "$got" = "$want" ]; then
        printf 'ok   %s\n' "$name"
    else
        printf 'FAIL %s(退出码 %s,期望 %s)\n' "$name" "$st" "$want_st"
        printf '%s\n' "$got" | sed 's/^/     /'
        fail=$((fail + 1))
    fi
}

# b 名字 期望字节数 期望退出码 命令...（用来核末行有没有换行这种字节级的事）
b() {
    name=$1; want_n=$2; want_st=$3; shift 3
    "$@" >"$tmp" 2>/dev/null; st=$?
    n=$(wc -c <"$tmp" | tr -d ' ')
    if [ "$st" -eq "$want_st" ] && [ "$n" -eq "$want_n" ]; then
        printf 'ok   %s\n' "$name"
    else
        printf 'FAIL %s(%s 字节，期望 %s)\n' "$name" "$n" "$want_n"
        fail=$((fail + 1))
    fi
}

t basic          0 "$(printf 'boot ok\nboot ok again')" "$LRG" boot "$D/sample.log"
t opt-ni         0 "$(printf '3:ERROR: retry later\n5:error: give up')" "$LRG" -in error "$D/sample.log"
t opt-ci         0 "$(printf '%s:2\n%s:1' "$D/sample.log" "$D/notes.txt")" \
                 "$LRG" -ci error "$D/sample.log" "$D/notes.txt"
t no-match       1 "" "$LRG" zzz "$D/sample.log"
t count-0        1 "0" "$LRG" -c zzz "$D/sample.log"
t bad-opt        2 "" "$LRG" -x foo "$D/sample.log"
t no-args        2 "" "$LRG"
t empty-pat      0 "$(printf 'a\n\nb')" "$LRG" "" "$D/blank.txt"
t one-bad        2 "$(printf '%s:1' "$D/notes.txt")" "$LRG" -c error "$D/no-such" "$D/notes.txt"
t stdin-n        0 "$(printf '1:boot ok\n4:boot ok again')" "$LRG" -n boot < "$D/sample.log"
t big-count      0 "200000" "$LRG" -c a "$D/big.txt"
b no-trailing-nl 4 0 "$LRG" def "$D/nonl.txt"
b cross-buffer   70007 0 "$LRG" needle "$D/cross.txt"
b long-line      1048582 0 "$LRG" needle "$D/long.txt"

printf -- '---\n'
if [ "$fail" -eq 0 ]; then echo 'ALL PASS'; else echo "FAILURES: $fail"; fi
exit "$fail"
