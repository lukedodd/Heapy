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

// Flag to indicate our injected thread has finished it's work.
volatile bool injectedThreadFinished = false;
volatile bool exitRequested = false;
DWORD dllThreadId = -1;

const int numHooks = 128;

// Hook tables. (Lot's of static data, but it's the only way to do this.)
// std::mutex hookTableMutex;
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
	// std::lock_guard<std::mutex> lk(hookTableMutex);
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


BOOL TerminateOtherThreads(DWORD excludedThreadId)
{ 
	DWORD currentThreadId = GetCurrentThreadId();
	DWORD dwOwnerPID = GetCurrentProcessId();
	HANDLE hThreadSnap = INVALID_HANDLE_VALUE; 
	THREADENTRY32 te32; 

	// Take a snapshot of all running threads  
	hThreadSnap = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, dwOwnerPID ); 
	if(hThreadSnap == INVALID_HANDLE_VALUE) 
		return( FALSE ); 

	// Fill in the size of the structure before using it. 
	te32.dwSize = sizeof(THREADENTRY32 ); 

	// Retrieve information about the first thread,
	// and exit if unsuccessful
	if(!Thread32First( hThreadSnap, &te32 )) 
	{
		printf( TEXT("Thread32First") );  // Show cause of failure
		CloseHandle( hThreadSnap );     // Must clean up the snapshot object!
		return( FALSE );
	}

	// Now walk the thread list of the system and terminate everything but the
	// current thread and excludedThread.
	do 
	{ 
		if( te32.th32OwnerProcessID == dwOwnerPID && te32.th32ThreadID != currentThreadId && te32.th32ThreadID != excludedThreadId)
		{
			if(!TerminateThread(OpenThread(THREAD_TERMINATE, false, te32.th32ThreadID), 0))
				printf("Failed to terminate thread.\n");
			else
				printf("Terminated thread.\n");
		}
	} while( Thread32Next(hThreadSnap, &te32 ) );


	//  Don't forget to clean up the snapshot object.
	CloseHandle( hThreadSnap );
	printf("Finished terminating threads..\n");
	return( TRUE );
}

// Hooks for process exit/termination.
// Used so our injected thread can be informed of the exit and have us wait until it's finished.
BOOL (WINAPI *terminateProcessOriginal)(HANDLE, UINT) = NULL;
BOOL WINAPI terminateProcessHook(HANDLE hProcess, UINT uExitCode){
	printf("Hooked terminate process!\n");

	TerminateOtherThreads(dllThreadId);

	exitRequested = true;
	while(!injectedThreadFinished)
		Sleep(50);

	return terminateProcessOriginal(hProcess, uExitCode);
}

VOID (WINAPI *exitProcessOriginal)(UINT) = NULL;
VOID WINAPI exitProcessHook(_In_ UINT uExitCode){
	printf("Hooked exit process!\n");

	// We're going to want to hang around but we should terminate
	// all other threads in the application apart from the current and injected dll thread.
	// 
	// Why?
	//
	// Lots of applications might not wait for threads to finish or terminate 
	// them prior to exiting. They wont crash because these threads are terminated
	// by ExitProcess before they have time to access any deconstructed data.
	// 
	// This is pretty messy but I'm not sure that we can do better.
	// I expect in the real wold Heapy will mostly be used while applications are running
	// and the leak detection on shutdown wont be useful on many apps.
	TerminateOtherThreads(dllThreadId);

	exitRequested = true;
	while(!injectedThreadFinished)
		Sleep(50);

	return exitProcessOriginal(uExitCode);
}

extern "C"{

// Our injected thread is made to call this function by EasyHook.
__declspec(dllexport) void __stdcall NativeInjectionEntryPoint(REMOTE_ENTRY_INFO* InRemoteInfo){
	printf("Injecting library...\n");
	nUsedMallocHooks = 0;
	nUsedFreeHooks = 0;

	PreventEverProfilingThisThread();
	dllThreadId = GetCurrentThreadId();

	// Create our hook pointer tables using template meta programming fu.
	InitNHooks<numHooks>::initHook(); 

	// Init min hook framework.
	MH_Initialize(); 

	// Init dbghelp framework.
	if(!SymInitialize(GetCurrentProcess(), NULL, true))
		printf("SymInitialize failed\n");

	heapProfiler = new HeapProfiler();

	// Trawl though loaded modules and hook any mallocs and frees we find.
	SymEnumerateModules(GetCurrentProcess(), enumModulesCallback, NULL);

	if(MH_CreateHook((void*)TerminateProcess, (void*)terminateProcessHook, (void**)&terminateProcessOriginal) != MH_OK)
		printf("Unable to hook TerminateProcess\n");
	if(MH_EnableHook(TerminateProcess) != MH_OK)
		printf("Unable to hook TerminateProcess\n");

	if(MH_CreateHook((void*)ExitProcess, (void*)exitProcessHook, (void**)&exitProcessOriginal) != MH_OK)
		printf("Unable to hook ExitProcess\n");
	if(MH_EnableHook(ExitProcess) != MH_OK)
		printf("Unable to hook ExitProcess\n");


	printf("Starting hooked application...\n");
	RhWakeUpProcess();

	// We can do what we want in this thread now but the target application knows nothing about it.
	// Therfore we need to be sneeky to avoid this thread just being terminated when the application exits.
	// That's why we hooked ExitProcess above to not do anything until we set injectedThreadFinished to true. 

	// Wait until app exits.
	while(!exitRequested){
		Sleep(5000);
		std::vector<std::pair<StackTrace, size_t>> allocsSortedBySize;
		heapProfiler->getAllocsSortedBySize(allocsSortedBySize);
		for(int i = 0; i < 10; ++i){
			printf("Alloc size %d\n", allocsSortedBySize[i].second);
			allocsSortedBySize[i].first.print();
		}
	}

	// getchar();
	// Finally let the target program actually exit!
	injectedThreadFinished = true; 
}

}