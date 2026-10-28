#pragma once
#define RTLD_NOLOAD 4
#define RTLD_NOW 2
extern "C" void* dlopen(const char*, int);
extern "C" void* dlsym(void*, const char*);
extern "C" char* dlerror();
