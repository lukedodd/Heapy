#include <stdio.h>
#include <mutex>

#include "easyhook.h"
#include "MinHook.h"
#include "dbghelp.h"

typedef void * (__cdecl *PtrMalloc)(size_t);
typedef void (__cdecl *PtrFree)(void *);

static const int numHooks = 128;

// Hook tables. (Lot's of static data, but it's the only way to do this.)
std::mutex hookTableMutex;
int nUsedMallocHooks = 0; 
int nUsedFreeHooks = 0; 
PtrMalloc mallocHooks[numHooks];
PtrFree freeHooks[numHooks];
PtrMalloc originalMallocs[numHooks];
PtrFree originalFrees[numHooks];
// TODO?: Special case of debug build malloc/frees?

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
		printf("Hooked malloc \n");
	}

	return p;
}

// Free hook function.
template <int N>
void  __cdecl freeHook(void * p){
	PreventSelfProfile preventSelfProfile;

	originalFrees[N](p);
	if(preventSelfProfile.shouldProfile()){
		printf("Hooked free %d\n", N);
	}
}

// Template recursion to init a hook table.
template<int N> struct InitNHooks{
    static void initHook(){
        InitNHooks<N-1>::initHook();  // Compile time recursion. 
        // printf("Initing hook %d \n", N);

		mallocHooks[N-1] = &mallocHook<N>;
		freeHooks[N-1] = &freeHook<N>;
    }
};
 
template<> struct InitNHooks<1>{
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
	// printf("EnumModuleCallback %s \n", ModuleName);
	SymEnumSymbols(GetCurrentProcess(), BaseOfDll, "malloc", enumSymbolsCallback, (void*)ModuleName);
	SymEnumSymbols(GetCurrentProcess(), BaseOfDll, "free", enumSymbolsCallback, (void*)ModuleName);
	return true;
}

extern "C"{

__declspec(dllexport) void __stdcall NativeInjectionEntryPoint(REMOTE_ENTRY_INFO* InRemoteInfo){
	printf("Injecting library...\n");

	PreventEverProfilingThisThread();

	// Create our hook pointer tables using template meta programming fu.
	InitNHooks<numHooks-1>::initHook(); 

	// Init min hook framework.
	MH_Initialize(); 

	// Init dbghelp framework.
	if(!SymInitialize(GetCurrentProcess(), NULL, true))
		printf("SymInitialize failed\n");

	// Trawl though loaded modules and hook any mallocs and frees we find.
	SymEnumerateModules(GetCurrentProcess(), enumModulesCallback, NULL);

	printf("Starting hooked application...\n");
	RhWakeUpProcess();
	for(;;)
		Sleep(3000);

	// Need to somehow uninstall hooks during the shutdown of this thread.
	// Although keeping it alive does not seem to cause many problems?
	// I don't know....
}

}