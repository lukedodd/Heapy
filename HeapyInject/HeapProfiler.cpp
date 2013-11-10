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

	// Print out stack trace. Skip the first frame (that's our hook function.)
	for(size_t i = 1; i < backtraceSize; ++i){ 
		if(backtrace[i]){
			// Output stack frame symbols if available.
			if(SymGetSymFromAddr(process, (DWORD64)backtrace[i], 0, symbol)){

				stream << "    " << symbol->Name;

				// Output filename + line info if available.
				IMAGEHLP_LINE lineSymbol = {0};
				lineSymbol.SizeOfStruct = sizeof(IMAGEHLP_LINE);
				DWORD displacement;
				if(SymGetLineFromAddr(process, (DWORD64)backtrace[i], &displacement, &lineSymbol)){
					stream << "    " << lineSymbol.FileName << ":" << lineSymbol.LineNumber;
				}
				

				stream << "    (" << std::setw(sizeof(void*)*2) << std::setfill('0') << backtrace[i] <<  ")\n";
			}else{
				stream << "    <no symbol> " << "    (" << std::setw(sizeof(void*)*2) << std::setfill('0') << backtrace[i] <<  ")\n";
			}
		}else{
			break;
		}
	}
}

void HeapProfiler::malloc(void *ptr, size_t size, const StackTrace &trace){
	std::lock_guard<std::mutex> lk(mutex);

	// Locate or create this stacktrace in the allocations map.
	if(stackTraces.find(trace.hash) == stackTraces.end()){
		stackTraces[trace.hash].trace = trace;
	}

	// Store the size for this allocation this stacktraces allocation map.
	stackTraces[trace.hash].allocations[ptr] = size;

	// Store the stracktrace hash of this allocation in the pointers map.
	ptrs[ptr] = trace.hash;
}

void HeapProfiler::free(void *ptr, const StackTrace &trace){
	std::lock_guard<std::mutex> lk(mutex);

	// On a free we remove the pointer from the ptrs map and the
	// allocating stack traces map.
	auto it = ptrs.find(ptr);
	if(it != ptrs.end()){
		StackHash stackHash = it->second;
		stackTraces[stackHash].allocations.erase(ptr); 
		ptrs.erase(it);
	}else{
		// Do anything with wild pointer frees?
	}
}

void HeapProfiler::getAllocationSiteReport(std::vector<std::pair<StackTrace, size_t>> &allocs){
	std::lock_guard<std::mutex> lk(mutex);
	allocs.clear();

	// For each allocation point.
	for(auto &traceInfo : stackTraces){
		// Sum up the size of all the allocations made.
		size_t sumOfAlloced = 0;
		for(auto &alloc : traceInfo.second.allocations)
			sumOfAlloced += alloc.second;

		// Add to alloation site report.
		allocs.push_back(std::make_pair(traceInfo.second.trace, sumOfAlloced));
	}
}