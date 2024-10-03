# xsp

A simple & fast hex searching and patching tool

## build

```shell
git clone https://github.com/Antibioticss/xsp.git --recursive
cd xsp
cmake . && cmake --build .
```

## usage

```
xsp - hex search & patch tool
usage: xsp [options] hex1 [hex2]
options:
  -f, --file <file>         path to the file to patch
  -r, --range <range>       range of the matches, eg: '0,-1'
  --str                     treat args as string instead of hex string
  -h, --help                print this usage
```

When `hex2` is not provided, the program will search `hex1` in the binary and print offsets.

Otherwise, it will replace the occurrences of `hex1` with `hex2`.

### Notes

All kinds of hex strings are supported, these are all valid.

```
abcd1234
ab cd 12 34
ABCD1234
AB cd 1234
```

`--range` uses Python-like indexes, start with 0, support negative indexes

```
elements: 12 34 ab cd
indexes:   0  1  2  3
indexes:  -4 -3 -2 -1
```