# Schema Guide

Hex Master supports a compact schema DSL for describing binary structures and applying them at a chosen base offset.

## Core shape

Each schema contains:

- `endian little` or `endian big`
- zero or more `struct Name { ... }` blocks
- one `root Name { ... }` block

Example:

```txt
endian little

struct Entry {
  u32 id
  u16 flags
  u16 value
}

root Header {
  bytes[4] magic
  u16 version
  u16 entry_count
  Entry[entry_count] entries
}
```

## Supported field types

Scalar types:

- `u8`, `u16`, `u32`, `u64`
- `i8`, `i16`, `i32`, `i64`
- `f32`, `f64`

Other supported forms:

- `bytes[n] name`
- `Type[count] name`
- `Type[count] name @offset_field`

## Example with offset-based array

```txt
endian little

struct Entry {
  u32 id
  u16 flags
  u16 value
}

root Header {
  bytes[4] magic
  u16 version
  u16 entry_count
  u32 entries_offset
  Entry[entry_count] entries @entries_offset
}
```

This means:

- `entry_count` controls how many `Entry` items are parsed
- `entries_offset` gives the file-relative offset, from the chosen base offset, where the array begins

## Current limits

The current schema engine is intentionally narrow:

- dependent references must target earlier scalar fields in the same scope
- no dotted paths yet
- no unions or conditional fields yet
- no arbitrary expressions yet

Use the in-app `Tools > Schema Editor...` window to create, open, save, validate, and run schemas.
