#include <stdio.h>
#include <mutex>
#include <vector>
#include <memory>

#include "HeapProfiler.h"

#include "easyhook.h"
#include "MinHook.h"
#include "dbghelp.h"
#include <tlhelp32.h>

typedef void * (__cdecl *PtrMalloc)(size_t);
typedef void (__cdecl *PtrFree)(void *);


const int numHooks = 128;

// Hook tables. (Lot's of static data, but it's the only way to do this.)
std::mutex hookTableMutex;
int nUsedMallocHooks = 0; 
int nUsedFreeHooks = 0; 
PtrMalloc mallocHooks[numHooks];
PtrFree freeHooks[numHooks];
PtrMalloc originalMallocs[numHooks];
PtrFree originalFrees[numHooks];
// TODO?: Special case of debug build malloc/frees?

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
		StackTrace t;
		t.trace();
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
BOOL enumSymbolsCallback(PSYMBOL_INFO symbolInfo, ULONG symbolSize, PVOID userContext){
	std::lock_guard<std::mutex> lk(hookTableMutex);
	PreventSelfProfile preventSelfProfile;

	PCSTR moduleName = (PCSTR)userContext;
	
	// Hook mallocs.
	if(strcmp(symbolInfo->Name, "malloc") == 0){
		int hookN = nUsedMallocHooks;
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
BOOL enumModulesCallback(PCSTR ModuleName, DWORD64 BaseOfDll, PVOID UserContext){
	// TODO: Hooking msvcrt causes problems with cleaning up stdio - avoid for now.
	if(strcmp(ModuleName, "msvcrt") == 0) 
		return true;

	SymEnumSymbols(GetCurrentProcess(), BaseOfDll, "malloc", enumSymbolsCallback, (void*)ModuleName);
	SymEnumSymbols(GetCurrentProcess(), BaseOfDll, "free", enumSymbolsCallback, (void*)ModuleName);
	return true;
}

void PrintTopAllocationReport(int numToPrint){
	std::vector<std::pair<StackTrace, size_t>> allocsSortedBySize;
	heapProfiler->getAllocationSiteReport(allocsSortedBySize);

	// Sort retured allocation sites by size of memory allocated, descending.
	std::sort(allocsSortedBySize.begin(), allocsSortedBySize.end(), [](const std::pair<StackTrace, size_t> &a,
											   const std::pair<StackTrace, size_t> &b){
		return a.second > b.second;
	});
	
	// Print top allocations sites.
	for(int i = 0; i < (std::min)(size_t(numToPrint), allocsSortedBySize.size()); ++i){
		printf("Alloc size %d\n", allocsSortedBySize[i].second);
		allocsSortedBySize[i].first.print();
	}
}

// Do an allocation report on exit.
struct CatchExit{
	~CatchExit(){
		PreventSelfProfile p;
		PrintTopAllocationReport(10);
	}
};
CatchExit catchExit;

extern "C"{

// Our injected thread is made to call this function by EasyHook.
__declspec(dllexport) void __stdcall NativeInjectionEntryPoint(REMOTE_ENTRY_INFO* InRemoteInfo){
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

	printf("Starting hooked application...\n");
	RhWakeUpProcess();

	// Print an allocation report every 10 seconds while the application is running.
	while(true){
		PrintTopAllocationReport(10);
		Sleep(10000); 
	}
}

}