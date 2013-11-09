#pragma once
#include <vector>
#include <unordered_map>
#include <set>
#include <mutex>

const int backtraceSize = 64;
typedef unsigned long StackHash;

struct StackTrace{
	void *backtrace[backtraceSize];
	StackHash hash;

	StackTrace();
	void trace(); 
	void print() const;
};

class HeapProfiler{
public:
	void malloc(void *ptr, size_t size, const StackTrace &trace);
	void free(void *ptr, const StackTrace &trace);

	// Return a list of allocation sites (a particular stack trace) and the amount
	// of memory currently allocated by each site.
	void getAllocationSiteReport(std::vector<std::pair<StackTrace, size_t>> &allocs);
private:
	std::mutex mutex;
	struct TraceInfo{
		StackTrace trace;
		std::unordered_map<void *, size_t> allocations;
	};
	std::unordered_map<StackHash, TraceInfo> allocations;
	std::unordered_map<void*, StackHash> ptrs;

};