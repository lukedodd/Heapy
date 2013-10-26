#include <stdio.h>
#include "easyhook.h"
#include "MinHook.h"
#include <thread>
#include "dbghelp.h"


typedef void * (__cdecl *PtrToMalloc)(size_t);
typedef void (__cdecl *PtrToFree)(void *);


static const int numHooks = 128;

// Lot's of static data, but it's the only way to do this.
// TODO: proper locking of the data below.
volatile int nUsedMallocHooks = 0; 
volatile int nUsedFreeHooks = 0; 
PtrToMalloc mallocHooks[numHooks];
PtrToFree freeHooks[numHooks];
PtrToMalloc originalMallocs[numHooks];
PtrToFree originalFrees[numHooks];
// TODO: Special case of debug build malloc/frees?


static __declspec( thread ) int depthCount = 0;
template <int N>
void * __cdecl mallocHook(size_t size){
	depthCount++;

	void * p = originalMallocs[N](size);
	if(depthCount < 2){
		printf("Hooked malloc \n");
	}

	depthCount--;
	return p;
}

template <int N>
void  __cdecl freeHook(void * p){

	originalFrees[N](p);
	depthCount++;
	if(depthCount < 2){
		printf("Hooked free %d\n", N);
	}

	depthCount--;
}

BOOL enumSymbols(PCSTR symbolName, DWORD64 symbolAddress, ULONG symbolSize, PVOID userContext){
	depthCount++; // disable any malloc/free profiling during the hook process.
	PCSTR moduleName = (PCSTR)userContext;
	
	// Hook mallocs.
	if(strcmp(symbolName, "malloc") == 0){
		printf("Hooking malloc from module %s !\n", moduleName);
		if(MH_CreateHook((void*)symbolAddress, mallocHooks[nUsedMallocHooks],  (void **)&originalMallocs[nUsedMallocHooks]) != MH_OK){
			printf("Create hook malloc failed!\n");
		}

		if(MH_EnableHook((void*)symbolAddress) != MH_OK){
			printf("Enable malloc hook failed!\n");
		}

		nUsedMallocHooks++;
	}

	// Hook frees.
	if(strcmp(symbolName, "free") == 0){
		printf("Hooking free from module %s !\n", moduleName);
		if(MH_CreateHook((void*)symbolAddress, freeHooks[nUsedFreeHooks],  (void **)&originalFrees[nUsedFreeHooks]) != MH_OK){
			printf("Create hook free failed!\n");
		}

		if(MH_EnableHook((void*)symbolAddress) != MH_OK){
			printf("Enable free failed!\n");
		}

		nUsedFreeHooks++;
	}

	depthCount--;
	return true;
}



BOOL enumModules(PCSTR ModuleName, DWORD64 BaseOfDll, PVOID UserContext){
	printf("EnumModuleCallback %s \n", ModuleName);
	// if(strcmp(ModuleName, "msvcrt") != 0)
		SymEnumerateSymbols(GetCurrentProcess(), BaseOfDll, enumSymbols, (void*)ModuleName);
	return true;
}


template<int N> struct InitNHooks{
    static void initHook(){
        InitNHooks<N-1>::initHook();  // Compile time recursion. 
        printf("Initing hook %d \n", N);

		mallocHooks[N] = &mallocHook<N>;
		freeHooks[N] = &freeHook<N>;
    }

};
 
template<> struct InitNHooks<-1>{
    static void initHook(){
		// stop the recursion
    }
};

extern "C"{
__declspec(dllexport) void __stdcall NativeInjectionEntryPoint(REMOTE_ENTRY_INFO* InRemoteInfo){
	printf("Injecting library...\n");

	depthCount = 2; // Disable all hooks in our dll thread forever.

	// Create our hook pointer tables using template meta programming fu.
	InitNHooks<numHooks-1>::initHook(); 

	// Init min hook framework.
	MH_Initialize(); 

	// Init dbghelp framework.
	if(!SymInitialize(GetCurrentProcess(), NULL, true))
		printf("SymInitialize failed\n");

	// Trawl though loaded modules and hook any mallocs and frees we find.
	SymEnumerateModules(GetCurrentProcess(), enumModules, NULL);

	printf("Starting hooked application...\n");
	RhWakeUpProcess();
	for(;;)
		Sleep(3000);

	// Need to somehow uninstall hooks during the shutdown of this thread.
	// Although keeping it alive does not seem to cause many problems?
	// I don't know....
}
}