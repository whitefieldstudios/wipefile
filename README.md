# wipefile

A command-line tool that overwrites a file's contents with zero bytes, renames it to a random string to hide the original name, and then deletes it. You can also wipe just parts of a file (the beginning, the end, or ranges in the middle) and keep the file afterward.

> **Warning:** This tool permanently destroys data. Use `-n` (dry run) first to check what will happen. Use it at your own risk. White Field Studios is not responsible for any data loss.

## Building

You need a C++17 compiler (for `std::filesystem`).

```sh
# macOS / Linux
c++ -std=c++17 -O2 -o wipefile wipefile.cpp

# Windows (MSVC) - linking setargv.obj turns on wildcard expansion
cl /std:c++17 /O2 /EHsc wipefile.cpp setargv.obj

# Windows (MinGW)
g++ -std=c++17 -O2 -o wipefile.exe wipefile.cpp
```

## Usage

```
wipefile <filename1> [filename2 ...]
wipefile -k <filename1> [filename2 ...]
wipefile -w <options> <filename1> [filename2 ...]
wipefile -n <filename1> [filename2 ...]
wipefile --help | -h | /?
```

With no flags, the entire file is zeroed, renamed to a random name, and deleted. You can combine flags, for example `wipefile -n -w B,E -k file.bin`.

### Global flags

| Flag | Meaning |
|------|---------|
| `-k` | Keep the file. The file is neither renamed nor deleted, even if the `-w` options say otherwise. |
| `-n`, `--dry-run` | Show what would be done without changing anything. Each file is still opened for read/write, so missing files and permission problems are reported, but no bytes are written and nothing is renamed or deleted. |
| `-w <options>` | Wipe only certain parts of the file (see below). |
| `-h`, `--help`, `/?` | Show help. |

### `-w` standard syntax: `beg,end,del`

Three comma-separated integers, with no spaces:

- `beg`: the number of zero bytes to write at the beginning. `-1` means the entire file.
- `end`: the number of zero bytes to write at the end. This is ignored if `beg` is `-1`.
- `del`: `1` deletes the file after wiping, `0` keeps it.

### `-w` letter syntax

Letters are case-insensitive and separated by commas:

| Option | Meaning |
|--------|---------|
| `B#` | Zero `#` bytes at the beginning (default 1024). |
| `E#` | Zero `#` bytes at the end (default 1024). |
| `M#,#` | Zero a range in the middle. The first number is the 1-based start position. The second is the length (default 1024). `M` with no numbers centers a 1024-byte wipe in the file. You can repeat it, for example `M10,20,M100,50`. |
| `K` | Keep the file. Without `K`, the file is deleted. |

## Examples

```sh
wipefile data.bin                    # zero the whole file, scramble its name, delete it
wipefile -k data.bin                 # zero the whole file but keep it
wipefile -w B,E file1.txt file2.txt  # zero the first and last 1024 bytes of each file, then delete them
wipefile -w M40,20 -k text.txt       # zero 20 bytes starting at position 40, keep the file
wipefile -n *.log                    # preview what would happen to each matching file
```

## Wildcards

`wipefile` does not expand wildcards itself:

- **macOS / Linux:** the shell expands `*.log` before the program runs, so wildcards work.
- **Windows MSVC builds:** wildcards only work if the program is linked with `setargv.obj` (see Building).
- **Windows MinGW builds:** usually expand wildcards by default.

A wildcard can match more files than you expect, so run `wipefile -n <pattern>` first.

## Exit code

The exit code is `0` if every file succeeded and `1` if any error occurred. If one file fails, the remaining files are still processed.

## Limitations

- The tool makes one pass of zeros. On SSDs, copy-on-write filesystems (APFS, Btrfs, ZFS), journaling filesystems, and files covered by snapshots or backups, earlier copies of the data may still exist somewhere on the device.
- Renaming hides the original filename in the directory entry, but it does not erase every copy of the filename the filesystem may keep (for example in its journal).

## License

MIT. See the header of `wipefile.cpp`.
