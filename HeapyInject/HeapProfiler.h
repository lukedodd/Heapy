#pragma once
#include <ostream>
#include <vector>
#include <unordered_map>
#include <set>
#include <Windows.h>

const int backtraceSize = 64;
typedef unsigned long StackHash;

struct StackTrace{
	void *backtrace[backtraceSize];
	StackHash hash;

	StackTrace();
	void trace(); 
	void print(std::ostream &stream) const;
};

class HeapProfiler{
public:
	HeapProfiler();
	~HeapProfiler();
	void malloc(void *ptr, size_t size, const StackTrace &trace);
	void free(void *ptr, const StackTrace &trace);

	// Return a list of allocation sites (a particular stack trace) and the amount
	// of memory currently allocated by each site.
	void getAllocationSiteReport(std::vector<std::pair<StackTrace, size_t>> &allocs);
private:
	HANDLE mutex;
	typedef std::unordered_map<void *, size_t> TraceInfoAllocCollection_t;
	struct TraceInfo{
		StackTrace trace;
		TraceInfoAllocCollection_t allocations;
	};
	typedef std::unordered_map<StackHash, TraceInfo> StackTraceCollection_t;
	StackTraceCollection_t stackTraces;
	std::unordered_map<void*, StackHash> ptrs;
};
