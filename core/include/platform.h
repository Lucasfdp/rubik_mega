#ifndef PLATFORM_H
# define PLATFORM_H

# include <stddef.h>

/// Declares the three allocator functions core/ uses, instead of
/// including <stdlib.h> directly. Native builds (`make test`) link
/// libc's real malloc/calloc/free against these exact signatures; the
/// WASM build (`make wasm`) is a freestanding target with no libc at
/// all -- <stdlib.h> doesn't exist there -- and links
/// src/platform/wasm_alloc.c's bump allocator against the same
/// signatures instead. One header, two implementations, chosen by
/// which Makefile target is building.
void	*malloc(size_t size);
void	*calloc(size_t count, size_t size);
void	free(void *ptr);

/// Marks a function as part of the WASM build's public export surface
/// (what web/ calls across the boundary -- docs/en/03-architecture.md
/// S3). A no-op everywhere else: wasm-ld needs an explicit per-symbol
/// export (there is no reliable "export everything" flag that survived
/// -O2 in testing), but the native test build doesn't use exports at
/// all, and `__attribute__((export_name))` is meaningless -- and, under
/// -Werror, an ERROR -- on a non-wasm target.
# if defined(__wasm__)
#  define WASM_EXPORT(name) __attribute__((export_name(#name)))
# else
#  define WASM_EXPORT(name)
# endif

#endif
