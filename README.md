# xsp

A simple & fast hex searching and patching tool

Based on anchored_memchr, a stride-anchored substring search by [EshayDev](https://github.com/EshayDev) that advances by the pattern length and, at each anchor, uses a per-byte inverted index of the pattern to generate candidate alignments, verifying each with memcmp. 

## build

```shell
git clone https://github.com/Antibioticss/xsp.git --recursive
cd xsp
mkdir build && cd build
cmake .. && cmake --build .
```

## usage

```
xsp - hex search & patch tool
usage: xsp [options] hex1 [hex2]
options:
  -f, --file <file>         path to the file to patch
  -o, --offset <offset>     base offset to start search from (hex or decimal)
  -s, --skip <bytes>        skip bytes from base offset before search/patch
  -l, --limit <size>        limit search to this many bytes from start position
  -r, --range <range>       range of the matches, eg: '0,-1'
  -t <threads>              number of threads to use (default: auto)
  --str                     treat args as string instead of hex string
  --benchmark               run search performance benchmarks
  -h, --help                print this usage
```

When `hex2` is not provided, `xsp` will search `hex1` in the binary and print offsets.

Otherwise, `xsp` will replace the occurrences of `hex1` with `hex2`.

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

range string is two indexes separated by `,`

`xsp` will replace the occurrences start from the first index to the second index (include the beginning and ending)

## Integration with symp

`xsp` can be used in conjunction with `symp` for symbol-based patching:

```bash
# Find symbol offset with symp and use it as base offset for xsp
xsp --offset $(symp -q 'symbol_name' binary_file) -f binary_file hex1 hex2

# Example: Patch code at a specific symbol location
xsp --offset $(symp -q '_main' /usr/bin/example) -f /usr/bin/example "90 90 90 90" "CC CC CC CC"
```
