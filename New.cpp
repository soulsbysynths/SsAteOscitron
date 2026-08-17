//New.cpp  operator new/delete
//avr-libc's C++ runtime support does not provide these; Arduino's core supplies them
//(cores/arduino/new.cpp) which is why they were never needed here before. Ported as
//thin wrappers over malloc/free, matching the Arduino core's own implementation.

#include <stdlib.h>

void* operator new(size_t size)
{
	return malloc(size);
}

void operator delete(void* ptr)
{
	free(ptr);
}

void* operator new[](size_t size)
{
	return malloc(size);
}

void operator delete[](void* ptr)
{
	free(ptr);
}
