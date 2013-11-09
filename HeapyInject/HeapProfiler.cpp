#include "HeapProfiler.h"

#include <Windows.h>
#include <stdio.h>
#include "dbghelp.h"

#include <algorithm>
#include <iomanip>

StackTrace::StackTrace() : hash(0){
	memset(backtrace, 0, sizeof(void*)*backtraceSize);
}

void StackTrace::trace(){
	CaptureStackBackTrace(0, backtraceSize, backtrace, &hash);
}

void StackTrace::print(std::ostream &stream) const {
	HANDLE process = GetCurrentProcess();

	const int MAXSYMBOLNAME = 128 - sizeof(IMAGEHLP_SYMBOL);
	char symbol64_buf[sizeof(IMAGEHLP_SYMBOL) + MAXSYMBOLNAME] = {0};
	IMAGEHLP_SYMBOL *symbol = reinterpret_cast<IMAGEHLP_SYMBOL*>(symbol64_buf);
	symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL);
	symbol->MaxNameLength = MAXSYMBOLNAME - 1;

	for(int i = 0; i < backtraceSize; ++i){
		if(backtrace[i]){
			if(SymGetSymFromAddr(process, (DWORD64)backtrace[i], 0, symbol)){
				stream << "     " << symbol->Name << "    (" << 
				std::setw(sizeof(void*)*2) << std::setfill('0') << backtrace[i] <<  ")\n";
			}
		}else{
			break;
		}
	}
}

void HeapProfiler::malloc(void *ptr, size_t size, const StackTrace &trace){
	std::lock_guard<std::mutex> lk(mutex);

	if(allocations.find(trace.hash) == allocations.end()){
		allocations[trace.hash].trace = trace;
	}

	allocations[trace.hash].allocations[ptr] = size;
	ptrs[ptr] = trace.hash;
}

void HeapProfiler::free(void *ptr, const StackTrace &trace){
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

void HeapProfiler::getAllocationSiteReport(std::vector<std::pair<StackTrace, size_t>> &allocs){
	std::lock_guard<std::mutex> lk(mutex);
	allocs.clear();

	// For each allocation 
	for(auto &traceInfo : allocations){
		size_t sumOfAlloced = 0;
		for(auto &alloc : traceInfo.second.allocations)
			sumOfAlloced += alloc.second;

		allocs.push_back(std::make_pair(traceInfo.second.trace, sumOfAlloced));
	}
}