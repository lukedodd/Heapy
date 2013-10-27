#include "HeapProfiler.h"
#include <Windows.h>
#include <stdio.h>
#include "dbghelp.h"

StackTrace::StackTrace() : hash(0){
	memset(backtrace, 0, sizeof(void*)*backtraceSize);
}

void StackTrace::trace(){
	CaptureStackBackTrace(0, backtraceSize, backtrace, &hash);
}

void StackTrace::print() const {
	HANDLE process = GetCurrentProcess();

	const int MAXSYMBOLNAME = 128 - sizeof(IMAGEHLP_SYMBOL64);
	char symbol64_buf[sizeof(IMAGEHLP_SYMBOL64) + MAXSYMBOLNAME] = {0};
	IMAGEHLP_SYMBOL64 *symbol64 = reinterpret_cast<IMAGEHLP_SYMBOL64*>(symbol64_buf);
	symbol64->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
	symbol64->MaxNameLength = MAXSYMBOLNAME - 1;

	printf("Start stack trace print.\n");
	for(int i = 0; i < backtraceSize; ++i){
		if(backtrace[i]){
			if(SymGetSymFromAddr(process, (DWORD64)backtrace[i], 0, symbol64))
				printf("%08x, %s\n", backtrace[i], symbol64->Name);
		}else{
			break;
		}
	}

}
