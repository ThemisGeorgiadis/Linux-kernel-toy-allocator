# Toy Kernel Allocator

A simple fixed-size object allocator implemented inside the Linux kernel for educational purposes.

The allocator is built on top of Linux's buddy allocator and demonstrates many of the concepts behind kernel memory allocators such as **SLAB** and **SLUB**. Each allocated page is divided into fixed-size 64-byte objects that are tracked using allocator metadata.


---

## Features

* Fixed-size object allocator (64-byte objects)
* Uses the Linux buddy allocator through `alloc_page()`
* Supports contiguous multi-object allocations within a page
* First-fit allocation strategy
* Simple bitmap-based allocation tracking
* Metadata stored separately using `kmalloc()`
* Basic `toy_alloc()` / `toy_free()` interface
* Debug function for visualizing allocator state

---

## Allocator Design

```
Linux Kernel
      │
      ▼
 toy_alloc(size)
      │
      ▼
Search existing pages
      │
      ├───────────────┐
      │               │
      ▼               ▼
Contiguous space?   No space found
      │               │
      ▼               ▼
Mark objects      alloc_page()
as allocated          │
      │               ▼
      │         Create metadata
      │               │
      └──────► Return pointer
```

Each managed page contains 64 objects of 64 bytes each.

```
+---------------------------------------------------------------+
| Obj0 | Obj1 | Obj2 | Obj3 | Obj4 | ... | Obj63 |
+---------------------------------------------------------------+
```

---

## Data Structures

### `toy_page`

Represents one managed page.

```c
struct toy_page {
    struct page *page;
    void *kaddr;

    unsigned int free_obj_cnt;

    unsigned int obj_available[64];
    unsigned int obj_alloc_length[64];
};
```

Each page stores:

* pointer to the kernel `struct page`
* mapped kernel virtual address
* number of free objects
* bitmap indicating whether each object is free or used
* allocation length for the first object of every allocation

---

### `toy_pages_list`

All managed pages are stored in a linked list.

```
head
 │
 ▼
+---------+      +---------+      +---------+
| toy_page| ---> | toy_page| ---> | toy_page|
+---------+      +---------+      +---------+
```

---

## Allocation

An allocation request is rounded up to a multiple of 64 bytes.

For example,

```
200 bytes

↓

4 objects
```

The allocator then:

1. Searches every managed page.
2. Looks for the first contiguous run of free objects.
3. Marks those objects as allocated.
4. Returns the virtual address of the first object.

Example:

Before:

```
[F][F][F][F][F][F][F]
```

Allocate three objects:

```
[U][U][U][F][F][F][F]
```

If no page contains enough contiguous free space, a new page is obtained from the Linux page allocator and added to the allocator's page list.

---

## Free

`toy_free()` receives the pointer returned by `toy_alloc()`.

The allocator:

1. Finds which managed page owns the pointer.
2. Computes the object's index inside the page.
3. Reads the allocation length stored in `obj_alloc_length[]`.
4. Marks the entire allocation as free.

Example:

Allocation:

```
Index

0 1 2 3 4 5

[3][ ][ ][ ][ ][ ]
```

The first object stores the allocation size.

Calling

```c
toy_free(ptr);
```

marks objects 0, 1 and 2 as free again.

---

## Example State

```
=== Toy Allocator State ===

Page 0
Free objects: 57

[00] USED  allocation size: 3
[01] USED
[02] USED
[03] FREE
[04] FREE
...
```