

#ifndef UTILITY_VOLATILEMEM_H_
#define UTILITY_VOLATILEMEM_H_

void* malloc_volatile_PSP(size_t size);
void free_volatile_PSP(void* ptr);

inline void* malloc_volatile(size_t size)
{
	return malloc_volatile_PSP(size);
}

inline void free_volatile(void* ptr)
{
	free_volatile_PSP(ptr);
}

#endif // UTILITY_VOLATILEMEM_H_
