#pragma once
#include <vector>
#include <unordered_map>
#include <set>
#include <mutex>
#include <algorithm>

const int backtraceSize = 256;
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
	void malloc(void *ptr, size_t size, const StackTrace &trace){
		std::lock_guard<std::mutex> lk(mutex);

		if(allocations.find(trace.hash) == allocations.end()){
			allocations[trace.hash].trace = trace;
		}

		allocations[trace.hash].allocations[ptr] = size;
		ptrs[ptr] = trace.hash;
	}

	void free(void *ptr, const StackTrace &trace){
		std::lock_guard<std::mutex> lk(mutex);
		auto it = ptrs.find(ptr);
		if(it != ptrs.end()){
			StackHash stackHash = it->second;
			allocations[stackHash].allocations.erase(ptr); 
			ptrs.erase(it);
		}else{
			// Do anything with wild pointer frees?
		}
	}

	void getAllocsSortedBySize(std::vector<std::pair<StackTrace, size_t>> &allocs){
		std::lock_guard<std::mutex> lk(mutex);
		allocs.clear();

		// Accumulate the size of all allocation location.
		for(auto &traceInfo : allocations){
			size_t sumOfAlloced = 0;
			for(auto &alloc : traceInfo.second.allocations)
				sumOfAlloced += alloc.second;

			allocs.push_back(std::make_pair(traceInfo.second.trace, sumOfAlloced));
		}
		
		// Sort retured allcos in size, descending.
		std::sort(allocs.begin(), allocs.end(), [](const std::pair<StackTrace, size_t> &a,
												   const std::pair<StackTrace, size_t> &b){
			return a.second > b.second;
		});
	}
private:
	std::mutex mutex;
	struct TraceInfo{
		StackTrace trace;
		std::unordered_map<void *, size_t> allocations;
	};
	std::unordered_map<StackHash, TraceInfo> allocations;
	std::unordered_map<void*, StackHash> ptrs;

};