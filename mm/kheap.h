#ifndef KHEAP_H
#define KHEAP_H

#include <stddef.h>

/* Optional explicit init (kmalloc() will lazily init anyway). Call
 * pmm_init() first. */
void  kheap_init(void);

void *kmalloc(size_t size);
void  kfree(void *ptr);
void *kcalloc(size_t nmemb, size_t size);
void *krealloc(void *ptr, size_t size);

#endif
