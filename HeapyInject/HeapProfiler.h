#pragma once
#include <ostream>
#include <vector>
#include <unordered_map>
#include <set>
#include <mutex>
#include <Windows.h>

const int backtraceSize = 62;
typedef size_t StackHash;

struct StackTrace{
	void *backtrace[backtraceSize];
	StackHash hash;

	StackTrace();
	void trace(); 
	void print(std::ostream &stream) const;
};


// We define our own Mutex type since we can't use any standard library features inside parts of heapy.
struct Mutex{
	CRITICAL_SECTION criticalSection;
	Mutex(){
		InitializeCriticalSectionAndSpinCount(&criticalSection, 400);
	}
	~Mutex(){
		DeleteCriticalSection(&criticalSection);
	}
};

struct lock_guard{
	Mutex& mutex;
	lock_guard(Mutex& mutex) : mutex(mutex){
		EnterCriticalSection(&mutex.criticalSection);
	}
	~lock_guard(){
		LeaveCriticalSection(&mutex.criticalSection);
	}
};


class HeapProfiler{
public:
	HeapProfiler();
	~HeapProfiler();

	void malloc(void *ptr, size_t size, const StackTrace &trace);
	void free(void *ptr, const StackTrace &trace);

	struct CallStackInfo {
		StackTrace trace;
		size_t totalSize;
		size_t n;
	};

	// Return a list of allocation sites (a particular stack trace) and the amount
	// of memory currently allocated by each site.
	void getAllocationSiteReport(std::vector<CallStackInfo> &allocs);
private:
	Mutex mutex;

		struct PointerInfo {
		StackHash stack;
		size_t size;
	};

	std::unordered_map<StackHash, CallStackInfo> stackTraces;
	std::unordered_map<void*, PointerInfo> ptrs;

};