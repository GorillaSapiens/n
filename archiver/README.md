# n65ar

`n65ar` is a small standalone archiver for bundling `.o65` object files into a single `.a65` library file and unpacking them later.

## Build

```sh
make
```

## Usage

Create an archive:

```sh
./n65ar -c output.a65 obj1.o65 obj2.o65 ... objN.o65
```

Extract an archive:

```sh
./n65ar -x input.a65
```

## Archive format

The format is intentionally simple:

- 7-byte magic: `NAR65\0\1`
- repeated member records until end of file:
  - 16-bit little-endian file name length
  - 32-bit little-endian file size
  - raw file name bytes (basename only, no trailing NUL)
  - raw file contents

## Notes

- Member names are stored as basenames only, so extracting does not recreate directories.
- Extraction refuses member names containing `/` or `\\`.
- Files larger than 4 GiB are rejected.
- This is a dumb bundle format, not a smart indexed librarian like `ar`.
