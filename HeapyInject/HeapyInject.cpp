#include <stdio.h>
#include <mutex>
#include <vector>
#include <memory>
#include <numeric>
#include <thread>
#include <fstream>
#include <iomanip>
#include <algorithm>

#include "HeapProfiler.h"

#include "MinHook.h"
#include "dbghelp.h"
#include <tlhelp32.h>

typedef void * (__cdecl *PtrMalloc)(size_t);
typedef void (__cdecl *PtrFree)(void *);


// Hook tables. (Lot's of static data, but it's the only way to do this.)
const int numHooks = 128;
std::mutex hookTableMutex;
int nUsedMallocHooks = 0; 
int nUsedFreeHooks = 0; 
PtrMalloc mallocHooks[numHooks];
PtrFree freeHooks[numHooks];
PtrMalloc originalMallocs[numHooks];
PtrFree originalFrees[numHooks];
// TODO?: Special case for debug build malloc/frees?

HeapProfiler *heapProfiler;

// Mechanism to stop us profiling ourself.
static __declspec( thread ) int _depthCount = 0; // use thread local count

struct PreventSelfProfile{
	PreventSelfProfile(){
		_depthCount++;
	}
	~PreventSelfProfile(){
		_depthCount--;
	}

	inline bool shouldProfile(){
		return _depthCount <= 1;
	}
private:
	PreventSelfProfile(const PreventSelfProfile&){}
	PreventSelfProfile& operator=(const PreventSelfProfile&){}
};

void PreventEverProfilingThisThread(){
	_depthCount++;
}

// Malloc hook function. Templated so we can hook many mallocs.
template <int N>
void * __cdecl mallocHook(size_t size){
	PreventSelfProfile preventSelfProfile;

	void * p = originalMallocs[N](size);
	if(preventSelfProfile.shouldProfile()){
		StackTrace trace;
		trace.trace();
		heapProfiler->malloc(p, size, trace);
	}

	return p;
}

// Free hook function.
template <int N>
void  __cdecl freeHook(void * p){
	PreventSelfProfile preventSelfProfile;

	originalFrees[N](p);
	if(preventSelfProfile.shouldProfile()){
		StackTrace trace;
		trace.trace();
		heapProfiler->free(p, trace);
	}
}

// Template recursion to init a hook table.
template<int N> struct InitNHooks{
    static void initHook(){
        InitNHooks<N-1>::initHook();  // Compile time recursion. 

		mallocHooks[N-1] = &mallocHook<N-1>;
		freeHooks[N-1] = &freeHook<N-1>;
    }
};
 
template<> struct InitNHooks<0>{
    static void initHook(){
		// stop the recursion
    }
};

// Callback which recieves addresses for mallocs/frees which we hook.
BOOL CALLBACK enumSymbolsCallback(PSYMBOL_INFO symbolInfo, ULONG symbolSize, PVOID userContext){
	std::lock_guard<std::mutex> lk(hookTableMutex);
	PreventSelfProfile preventSelfProfile;

	PCSTR moduleName = (PCSTR)userContext;
	
	// Hook mallocs.
	if(strcmp(symbolInfo->Name, "malloc") == 0){
		if(nUsedMallocHooks >= numHooks){
			printf("All malloc hooks used up!\n");
			return true;
		}
		printf("Hooking malloc from module %s into malloc hook num %d.\n", moduleName, nUsedMallocHooks);
		if(MH_CreateHook((void*)symbolInfo->Address, mallocHooks[nUsedMallocHooks],  (void **)&originalMallocs[nUsedMallocHooks]) != MH_OK){
			printf("Create hook malloc failed!\n");
		}

		if(MH_EnableHook((void*)symbolInfo->Address) != MH_OK){
			printf("Enable malloc hook failed!\n");
		}

		nUsedMallocHooks++;
	}

	// Hook frees.
	if(strcmp(symbolInfo->Name, "free") == 0){
		if(nUsedFreeHooks >= numHooks){
			printf("All free hooks used up!\n");
			return true;
		}
		printf("Hooking free from module %s into free hook num %d.\n", moduleName, nUsedFreeHooks);
		if(MH_CreateHook((void*)symbolInfo->Address, freeHooks[nUsedFreeHooks],  (void **)&originalFrees[nUsedFreeHooks]) != MH_OK){
			printf("Create hook free failed!\n");
		}

		if(MH_EnableHook((void*)symbolInfo->Address) != MH_OK){
			printf("Enable free failed!\n");
		}

		nUsedFreeHooks++;
	}

	return true;
}

// Callback which recieves loaded module names which we search for malloc/frees to hook.
BOOL CALLBACK enumModulesCallback(PCSTR ModuleName, DWORD_PTR BaseOfDll, PVOID UserContext){
	// TODO: Hooking msvcrt causes problems with cleaning up stdio - avoid for now.
	if(strcmp(ModuleName, "msvcrt") == 0) 
		return true;

	SymEnumSymbols(GetCurrentProcess(), BaseOfDll, "malloc", enumSymbolsCallback, (void*)ModuleName);
	SymEnumSymbols(GetCurrentProcess(), BaseOfDll, "free", enumSymbolsCallback, (void*)ModuleName);
	return true;
}

void printTopAllocationReport(int numToPrint){

	std::vector<std::pair<StackTrace, size_t>> allocsSortedBySize;
	heapProfiler->getAllocationSiteReport(allocsSortedBySize);

	// Sort retured allocation sites by size of memory allocated, descending.
	std::sort(allocsSortedBySize.begin(), allocsSortedBySize.end(), 
		[](const std::pair<StackTrace, size_t> &a, const std::pair<StackTrace, size_t> &b){
			return a.second < b.second;
		}
	);
	

	std::ofstream stream("Heapy_Profile.txt",  std::ios::out | std::ios::app);
	stream << "=======================================\n\n";
	stream << "Printing top allocation points.\n\n";
	// Print top allocations sites in ascending order.
	auto precision = std::setprecision(5);
	size_t totalPrintedAllocSize = 0;
	size_t numPrintedAllocations = 0;
	double bytesInAMegaByte = 1024*1024;
	for(size_t i = (size_t)(std::max)(int64_t(allocsSortedBySize.size())-numToPrint, int64_t(0)); i < allocsSortedBySize.size(); ++i){

		if(allocsSortedBySize[i].second == 0)
			continue;

		stream << "Alloc size " << precision << allocsSortedBySize[i].second/bytesInAMegaByte << "Mb, stack trace: \n";
		allocsSortedBySize[i].first.print(stream);
		stream << "\n";

		totalPrintedAllocSize += allocsSortedBySize[i].second;
		numPrintedAllocations++;
	}

	size_t totalAlloctaions = std::accumulate(allocsSortedBySize.begin(), allocsSortedBySize.end(), size_t(0),
		[](size_t a,  const std::pair<StackTrace, size_t> &b){
			return a + b.second;
		}
	);

	stream << "Top " << numPrintedAllocations << " allocations: " << precision <<  totalPrintedAllocSize/bytesInAMegaByte << "Mb\n";
	stream << "Total allocations: " << precision << totalAlloctaions/bytesInAMegaByte << "Mb" << 
		" (difference between printed and top " << numPrintedAllocations << " allocations : " << (totalAlloctaions - totalPrintedAllocSize)/bytesInAMegaByte << "Mb)\n\n";
}

// Do an allocation report on exit.
// Static data deconstructors are supposed to be called in reverse order of the construction.
// (According to the C++ spec.)
// 
// So this /should/ be called after the staic deconstructors of the injectee application.
// Probably by whatever thread is calling exit. 
// 
// We are well out of the normal use cases here and I wouldn't be suprised if this mechanism
// breaks down in practice. I'm not even that interested in leak detection on exit anyway.
// 
// Also: The end game will be send malloc/free information to a different
// process instead of doing reports the same process - then shutdown issues go away.
// But for now it's more fun to work inside the injected process.
struct CatchExit{
	~CatchExit(){
		PreventSelfProfile p;
		printTopAllocationReport(25);
	}
};
CatchExit catchExit;

int heapProfileReportThread(){
	PreventEverProfilingThisThread();
	while(true){
		Sleep(10000); 
		printTopAllocationReport(25);
	}
}

void setupHeapProfiling(){
	// We use printfs thoughout injection becasue it's just safer/less troublesome
	// than iostreams for this sort of low-level/hacky/threaded work.
	printf("Injecting library...\n");

	nUsedMallocHooks = 0;
	nUsedFreeHooks = 0;

	PreventEverProfilingThisThread();

	// Create our hook pointer tables using template meta programming fu.
	InitNHooks<numHooks>::initHook(); 

	// Init min hook framework.
	MH_Initialize(); 

	// Init dbghelp framework.
	if(!SymInitialize(GetCurrentProcess(), NULL, true))
		printf("SymInitialize failed\n");

	// Yes this leaks - cleauing it up at application exit has zero real benefit.
	// Might be able to clean it up on CatchExit but I don't see the point.
	heapProfiler = new HeapProfiler(); 

	// Trawl though loaded modules and hook any mallocs and frees we find.
	SymEnumerateModules(GetCurrentProcess(), enumModulesCallback, NULL);

	// Spawn and a new thread which prints allocation report every 10 seconds.
	//
	// We can't use std::thread here because of deadlock issues which can happen 
	// when creating a thread in dllmain.
	// Some background: http://blogs.msdn.com/b/oldnewthing/archive/2007/09/04/4731478.aspx
	//
	// We have to create a new thread because we "signal" back to the injector that it is
	// safe to resume the injectees main thread by terminating the injected thread.
	// (The injected thread ran LoadLibrary so got us unti DllMain DLL_PROCESS_ATTACH.)
	//
	// TODO: Could we signal a different way, or awake the main thread from the dll thread
	// and do the reports from the injeted thread. This was what EasyHook was doing.
	// I feel like that might have some benefits (more stable?)
	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)&heapProfileReportThread, NULL, 0, NULL);
}

extern "C"{

BOOL APIENTRY DllMain(HANDLE hModule, DWORD reasonForCall, LPVOID lpReserved){
	switch (reasonForCall){
		case DLL_PROCESS_ATTACH:
			setupHeapProfiling();
		break;
		case DLL_THREAD_ATTACH:
		break;
		case DLL_THREAD_DETACH:
		break;
		case DLL_PROCESS_DETACH:
		break;
	}

	return TRUE;
}

}