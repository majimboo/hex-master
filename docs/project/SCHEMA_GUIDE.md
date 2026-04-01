# Schema Guide

Hex Master supports a compact schema DSL for describing binary structures and applying them at a chosen base offset.

After a schema runs, the structure tree shows parsed offsets, sizes, and values. Selecting a field in the tree highlights the corresponding byte range in the main hex view.
The Schema Editor can also export the parsed result tree to JSON.

## Overview

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

The base offset is chosen when you run the schema. Fields are parsed in order, and later fields can refer to earlier scalar fields in the same scope.

## Staged layouts and repeat blocks

The DSL also supports staged parsing where one part of the file describes a later section.

Form:

- `repeat alias in sourcePath { ... }`

Inside a repeat block:

- `alias` refers to the current item from the source array
- dotted references such as `alias.faceCount` or `meshHeader.subHeaders` are allowed
- repeated items are parsed in file order at the current cursor position

Example:

```txt
root Example {
  u32 meshCount
  MeshHeader[meshCount] headers

  repeat meshHeader in headers {
    repeat subHeader in meshHeader.subHeaders {
      Face[subHeader.faceCount] faces
      f32[subHeader.vertexCount] posX
      f32[subHeader.vertexCount] posY
      f32[subHeader.vertexCount] posZ
    }
  }
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
- `Type[count_expression] name`
- `Type[count_expression] name @offset_expression`

## Quick reference

- `u32 count`
  Reads one scalar value.
- `bytes[32] blob`
  Reads a fixed-size byte block.
- `Entry[count] entries`
  Reads an array using an earlier scalar field.
- `Entry[count] entries @entries_offset`
  Reads an array from a different offset.
- `Entry[count * 2] entries @base + 0x20`
  Uses expressions in both count and offset.

## Structure tree interaction

After a successful run:

- click a field in the structure tree to jump the main hex view to that byte range
- use the tree to inspect parsed offsets, sizes, and interpreted values
- use the main hex editor itself for byte-level modifications

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

## Expressions

Count and offset fields now accept simple arithmetic expressions:

- `numBones * numFrames`
- `count + 1`
- `entries_offset + 0x20`
- `(header_size + table_size) * 2`

Expressions can use:

- earlier scalar fields in the same scope
- dotted references inside repeat scopes such as `subHeader.vertexCount`
- decimal and hexadecimal integer literals
- `+`, `-`, `*`, `/`
- parentheses

## Real-world example

This pattern works for binary animation data where one array length depends on two earlier fields:

```txt
endian little

struct Transform {
  f32 r00
  f32 r01
  f32 r02
  f32 r10
  f32 r11
  f32 r12
  f32 r20
  f32 r21
  f32 r22
  f32 px
  f32 py
  f32 pz
}

struct Action {
  u16 numBones
  u16 numFrames
  Transform[numBones * numFrames] transforms
}

struct Actor {
  u16 numAction
  Action[numAction] actions
}

root CharAni {
  u32 numActors
  Actor[numActors] actors
}
```

## Real-world staged example

`item.obj`-style staged header/body layouts can now be described with repeat blocks:

```txt
endian little

struct SubHeader {
  i32 faceCount
  i32 vertexCount
}

struct MeshHeader {
  i32 subMeshCount
  SubHeader[subMeshCount] subHeaders
}

struct Face {
  u16 a
  u16 b
  u16 c
}

root ItemObj {
  i32 numMeshes
  MeshHeader[numMeshes] headers

  repeat meshHeader in headers {
    repeat subHeader in meshHeader.subHeaders {
      Face[subHeader.faceCount] faces
      f32[subHeader.vertexCount] posX
      f32[subHeader.vertexCount] posY
      f32[subHeader.vertexCount] posZ
    }
  }
}
```

See [docs/examples/item_obj.schema](/D:/projects/hex_master/docs/examples/item_obj.schema) for the fuller sample.

## Current limits

The current schema engine is intentionally narrow:

- dependent references must target earlier scalar fields in the same scope
- no unions or conditional fields yet
- no arbitrary random-access pointer chasing outside supported offset expressions and repeat scopes
- no named enums or bitfields yet

Use the in-app `Tools > Schema Editor...` window to create, open, save, validate, run, and export schemas.
