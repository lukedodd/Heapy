#include "HeapProfiler.h"

#include <Windows.h>
#include <stdio.h>
#include "dbghelp.h"

#include <algorithm>
#include <iomanip>
#include <thread>


StackTrace::StackTrace() : hash(0){
	memset(backtrace, 0, sizeof(void*)*backtraceSize);
}

void StackTrace::trace(){
	int framesCnt = CaptureStackBackTrace(0, backtraceSize, backtrace, 0);
	// Compute simple polynomial hash of the stack trace.
	// Note: CaptureStackBackTrace returns plain sum of all pointers as BackTraceHash.
	const size_t BASE = sizeof(size_t) > 4 ? 11400714819323198485ULL : 2654435769U;
	hash = 0;
	for (int i = 0; i < framesCnt; i++)
		hash = hash * BASE + (size_t)backtrace[i];
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

HeapProfiler::HeapProfiler(){

}

HeapProfiler::~HeapProfiler(){

}

void HeapProfiler::malloc(void *ptr, size_t size, const StackTrace &trace){
	lock_guard lk(mutex);

	if (ptrs.find(ptr) != ptrs.end())
	{
		//two buffers at same address!
		//heap overflow?
		return;
	}

	// Locate or create this stacktrace in the allocations map.
	if(stackTraces.find(trace.hash) == stackTraces.end()){
		CallStackInfo &stack = stackTraces[trace.hash];
		stack.trace = trace;
		stack.totalSize = 0;
		stack.n = 0;
	}

	// Store the size for this allocation this stacktraces allocation map.
	auto& callStackInfo = stackTraces[trace.hash];
	callStackInfo.totalSize += size;
	callStackInfo.n++;

	// Store the stracktrace hash of this allocation in the pointers map.
	PointerInfo &ptrInfo = ptrs[ptr];
	ptrInfo.size = size;
	ptrInfo.stack = trace.hash;
}

void HeapProfiler::free(void *ptr, const StackTrace &trace){
	lock_guard lk(mutex);

	// On a free we remove the pointer from the ptrs map and the
	// allocating stack traces map.
	auto it = ptrs.find(ptr);
	if(it != ptrs.end()){
		const PointerInfo &info = it->second;
		stackTraces[info.stack].totalSize -= info.size;
		ptrs.erase(it);
	}else{
		// Do anything with wild pointer frees?
	}
}

void HeapProfiler::getAllocationSiteReport(std::vector<CallStackInfo> &allocs){
	lock_guard lk(mutex);

	allocs.resize(stackTraces.size());

	for(auto& p : stackTraces){
		allocs.emplace_back(p.second);
	}

}
