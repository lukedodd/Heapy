#include <vector>
#include <memory>
#include <numeric>
#include <fstream>
#include <iomanip>
#include <algorithm>

#include "HeapProfiler.h"

#include "MinHook.h"
#include "dbghelp.h"
#include <tlhelp32.h>

typedef __int64 int64_t;
typedef void * (__cdecl *PtrMalloc)(size_t);
typedef void (__cdecl *PtrFree)(void *);
typedef void* (__cdecl *PtrRealloc)(void *, size_t);
typedef void * (__cdecl *PtrCalloc)(size_t, size_t);

struct Mutex
{
	HANDLE m_hMutex;
	Mutex(): m_hMutex(CreateMutex(NULL, FALSE, NULL)){
	}
	~Mutex(){
		if (m_hMutex != NULL)
			CloseHandle(m_hMutex);
	}
};

struct lock_guard
{
	Mutex& m_Mutex;
	lock_guard(Mutex& hMutex) : m_Mutex(hMutex){
		WaitForSingleObject(m_Mutex.m_hMutex, INFINITE);
	}
	~lock_guard(){
		ReleaseMutex(m_Mutex.m_hMutex);
	}
};

// Hook tables. (Lot's of static data, but it's the only way to do this.)
const int numHooks = 128;
Mutex hookTableMutex;
int nUsedMallocHooks = 0;
int nUsedFreeHooks = 0;
int nUsedReallocHooks = 0;
int nUsedCallocHooks = 0;
PtrMalloc mallocHooks[numHooks];
PtrFree freeHooks[numHooks];
PtrRealloc reallocHooks[numHooks];
PtrCalloc callocHooks[numHooks];
PtrMalloc originalMallocs[numHooks];
PtrFree originalFrees[numHooks];
PtrRealloc originalReallocs[numHooks];
PtrCalloc originalCallocs[numHooks];
// TODO?: Special case for debug build malloc/frees?

HeapProfiler *heapProfiler;

// Mechanism to stop us profiling ourself.
DWORD tlsIndex;

struct PreventSelfProfile{
	PreventSelfProfile(){
		int depthCount = (int)TlsGetValue(tlsIndex);
		TlsSetValue(tlsIndex, (LPVOID)(depthCount+1));
	}
	~PreventSelfProfile(){
		int depthCount = (int)TlsGetValue(tlsIndex);
		TlsSetValue(tlsIndex, (LPVOID)(depthCount-1));
	}

	inline bool shouldProfile(){
		int depthCount = (int)TlsGetValue(tlsIndex);
		return depthCount <= 1;
	}
private:
	PreventSelfProfile(const PreventSelfProfile&){}
	PreventSelfProfile& operator=(const PreventSelfProfile&){}
};

void PreventEverProfilingThisThread(){
	int depthCount = (int)TlsGetValue(tlsIndex);
	TlsSetValue(tlsIndex, (LPVOID)(depthCount+1));
}

// Malloc hook function. Templated so we can hook many mallocs.
template <int N>
void * __cdecl mallocHook(size_t size){
	void * p;
	DWORD lastError;
	{
		PreventSelfProfile preventSelfProfile;

		p = originalMallocs[N](size);
		lastError = GetLastError();
		if(preventSelfProfile.shouldProfile()){
			StackTrace trace;
			trace.trace();
			heapProfiler->malloc(p, size, trace);
		}
	}
	SetLastError(lastError);

	return p;
}

// Free hook function.
template <int N>
void  __cdecl freeHook(void * p){
	DWORD lastError;
	{
		PreventSelfProfile preventSelfProfile;

		originalFrees[N](p);
		lastError = GetLastError();
		if(preventSelfProfile.shouldProfile()){
			StackTrace trace;
			//trace.trace();
			heapProfiler->free(p, trace);
		}
	}
	SetLastError(lastError);
}

// Realloc hook function. Templated so we can hook many mallocs.
template <int N>
void * __cdecl reallocHook(void* memblock, size_t size){
	void * p;
	DWORD lastError;
	{
		PreventSelfProfile preventSelfProfile;

		p = originalReallocs[N](memblock, size);
		lastError = GetLastError();
		if (memblock == NULL){
			// memblock == NULL -> call malloc()
			if(preventSelfProfile.shouldProfile()){
				StackTrace trace;
				trace.trace();
				heapProfiler->malloc(p, size, trace);
			}
		}
		else if (size == 0){
			// size == 0 -> call free()
			if(preventSelfProfile.shouldProfile()){
				StackTrace trace;
				//trace.trace();
				heapProfiler->free(memblock, trace);
			}
		}
		else if (p == NULL){
			// p == NULL -> no memory, memblock not touched
		}
		else {
			if(preventSelfProfile.shouldProfile()){
				StackTrace trace;
				//trace.trace();
				heapProfiler->free(memblock, trace);
				heapProfiler->malloc(p, size, trace);
			}
		}
	}
	SetLastError(lastError);

	return p;
}

// Calloc hook function. Templated so we can hook many Callocs.
template <int N>
void * __cdecl callocHook(size_t num, size_t size){
	void * p;
	DWORD lastError;
	{
		PreventSelfProfile preventSelfProfile;

		p = originalCallocs[N](num, size);
		lastError = GetLastError();
		if(preventSelfProfile.shouldProfile()){
			StackTrace trace;
			trace.trace();
			heapProfiler->malloc(p, num * size, trace);
		}
	}
	SetLastError(lastError);

	return p;
}

// Template recursion to init a hook table.
template<int N> struct InitNHooks{
	static void initHook(){
		InitNHooks<N-1>::initHook();  // Compile time recursion. 

		mallocHooks[N-1] = &mallocHook<N-1>;
		freeHooks[N-1] = &freeHook<N-1>;
		reallocHooks[N-1] = &reallocHook<N-1>;
		callocHooks[N-1] = &callocHook<N-1>;
	}
};
 
template<> struct InitNHooks<0>{
	static void initHook(){
		// stop the recursion
	}
};

// Internal function to reverse string buffer
static void internal_reverse(char str[], int length){
	int start = 0;
	int end = length -1;
	while (start < end){
		char c = str[start];
		str[start] = str[end];
		str[end] = c;

		start++;
		end--;
	}
}

// Internal itoa()
static char* internal_itoa(__int64 num, char* str, int base){
	int i = 0;
	bool isNegative = false;
 
	// Handle 0 explicitely, otherwise empty string is printed for 0
	if (num == 0){
		str[i++] = '0';
		str[i] = '\0';
		return str;
	}
 
	// In standard itoa(), negative numbers are handled only with 
	// base 10. Otherwise numbers are considered unsigned.
	if (num < 0 && base == 10){
		isNegative = true;
		num = -num;
	}
 
	// Process individual digits
	while (num != 0){
		int rem = num % base;
		str[i++] = (rem > 9)? (rem-10) + 'a' : rem + '0';
		num = num/base;
	}
 
	// If number is negative, append '-'
	if (isNegative)
		str[i++] = '-';

	str[i] = '\0'; // Append string terminator

	// Reverse the string
	internal_reverse(str, i);

	return str;
}

// Internal function to write inject log to InjectLog.txt
void InjectLog(const char* szStr1, const char* szStr2=NULL, const char* szStr3=NULL, const char* szStr4=NULL, const char* szStr5=NULL){
	HANDLE hFile = CreateFileA("InjectLog.txt", GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile != INVALID_HANDLE_VALUE){
		LARGE_INTEGER lEndOfFilePointer;
		LARGE_INTEGER lTemp;
		lTemp.QuadPart = 0;
		SetFilePointerEx(hFile, lTemp, &lEndOfFilePointer, FILE_END);

		DWORD dwByteWritten;
		WriteFile(hFile, szStr1, strlen(szStr1), &dwByteWritten, NULL);
		if (szStr2 != NULL)
			WriteFile(hFile, szStr2, strlen(szStr2), &dwByteWritten, NULL);
		if (szStr3 != NULL)
			WriteFile(hFile, szStr3, strlen(szStr3), &dwByteWritten, NULL);
		if (szStr4 != NULL)
			WriteFile(hFile, szStr4, strlen(szStr4), &dwByteWritten, NULL);
		if (szStr5 != NULL)
			WriteFile(hFile, szStr5, strlen(szStr5), &dwByteWritten, NULL);
		CloseHandle(hFile);
	}
}

// Callback which recieves addresses for mallocs/frees which we hook.
BOOL CALLBACK enumSymbolsCallback(PSYMBOL_INFO symbolInfo, ULONG symbolSize, PVOID userContext){
	lock_guard lk(hookTableMutex);
	PreventSelfProfile preventSelfProfile;

	PCSTR moduleName = (PCSTR)userContext;
	char logBuffer[30];

	// Hook mallocs.
	if(strcmp(symbolInfo->Name, "malloc") == 0){
		if(nUsedMallocHooks >= numHooks){
			InjectLog("All malloc hooks used up!\r\n");
			return true;
		}
		internal_itoa(nUsedMallocHooks, logBuffer, 10);
		InjectLog("Hooking malloc from module ", moduleName, " into malloc hook num ", logBuffer, ".\r\n");
		if(MH_CreateHook((void*)symbolInfo->Address, mallocHooks[nUsedMallocHooks],  (void **)&originalMallocs[nUsedMallocHooks]) != MH_OK){
			InjectLog("Create hook malloc failed!\r\n");
		}

		if(MH_EnableHook((void*)symbolInfo->Address) != MH_OK){
			InjectLog("Enable malloc hook failed!\r\n");
		}

		nUsedMallocHooks++;
	}

	// Hook frees.
	if(strcmp(symbolInfo->Name, "free") == 0){
		if(nUsedFreeHooks >= numHooks){
			InjectLog("All free hooks used up!\r\n");
			return true;
		}
		internal_itoa(nUsedFreeHooks, logBuffer, 10);
		InjectLog("Hooking free from module ", moduleName, " into free hook num ", logBuffer, ".\r\n");
		if(MH_CreateHook((void*)symbolInfo->Address, freeHooks[nUsedFreeHooks],  (void **)&originalFrees[nUsedFreeHooks]) != MH_OK){
			InjectLog("Create hook free failed!\r\n");
		}

		if(MH_EnableHook((void*)symbolInfo->Address) != MH_OK){
			InjectLog("Enable free failed!\r\n");
		}

		nUsedFreeHooks++;
	}

	// Hook reallocs.
	if(strcmp(symbolInfo->Name, "realloc") == 0){
		if(nUsedReallocHooks >= numHooks){
			InjectLog("All realloc hooks used up!\r\n");
			return true;
		}
		internal_itoa(nUsedReallocHooks, logBuffer, 10);
		InjectLog("Hooking realloc from module ", moduleName, " into realloc hook num ", logBuffer, ".\r\n");
		if(MH_CreateHook((void*)symbolInfo->Address, reallocHooks[nUsedReallocHooks],  (void **)&originalReallocs[nUsedReallocHooks]) != MH_OK){
			InjectLog("Create hook realloc failed!\r\n");
		}

		if(MH_EnableHook((void*)symbolInfo->Address) != MH_OK){
			InjectLog("Enable realloc hook failed!\r\n");
		}

		nUsedReallocHooks++;
	}

	// Hook Callocs.
	if(strcmp(symbolInfo->Name, "Calloc") == 0){
		if(nUsedCallocHooks >= numHooks){
			InjectLog("All Calloc hooks used up!\r\n");
			return true;
		}
		internal_itoa(nUsedCallocHooks, logBuffer, 10);
		InjectLog("Hooking Calloc from module ", moduleName, " into Calloc hook num ", logBuffer, ".\r\n");
		if(MH_CreateHook((void*)symbolInfo->Address, callocHooks[nUsedCallocHooks],  (void **)&originalCallocs[nUsedCallocHooks]) != MH_OK){
			InjectLog("Create hook Calloc failed!\r\n");
		}

		if(MH_EnableHook((void*)symbolInfo->Address) != MH_OK){
			InjectLog("Enable Calloc hook failed!\r\n");
		}

		nUsedCallocHooks++;
	}

	return true;
}

// Callback which recieves loaded module names which we search for malloc/frees to hook.
BOOL CALLBACK enumModulesCallback(PCSTR ModuleName, DWORD_PTR BaseOfDll, PVOID UserContext){
	HANDLE currentProcess = GetCurrentProcess();
	SymEnumSymbols(currentProcess, BaseOfDll, "malloc", enumSymbolsCallback, (void*)ModuleName);
	SymEnumSymbols(currentProcess, BaseOfDll, "free", enumSymbolsCallback, (void*)ModuleName);
	SymEnumSymbols(currentProcess, BaseOfDll, "realloc", enumSymbolsCallback, (void*)ModuleName);
	SymEnumSymbols(currentProcess, BaseOfDll, "calloc", enumSymbolsCallback, (void*)ModuleName);
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
		" (difference between total and top " << numPrintedAllocations << " allocations : " << (totalAlloctaions - totalPrintedAllocSize)/bytesInAMegaByte << "Mb)\n\n";
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
	// We use InjectLog() thoughout injection becasue it's just safer/less troublesome
	// than printf/iostreams for this sort of low-level/hacky/threaded work.
	InjectLog("Injecting library...\r\n");

	nUsedMallocHooks = 0;
	nUsedFreeHooks = 0;
	nUsedReallocHooks = 0;
	nUsedCallocHooks = 0;

	tlsIndex = TlsAlloc();
	TlsSetValue(tlsIndex, (LPVOID)0);
	PreventEverProfilingThisThread();

	// Create our hook pointer tables using template meta programming fu.
	InitNHooks<numHooks>::initHook(); 

	// Init min hook framework.
	MH_Initialize(); 

	// Init dbghelp framework.
	if(!SymInitialize(GetCurrentProcess(), NULL, true))
		InjectLog("SymInitialize failed\n");

	// Yes this leaks - cleauing it up at application exit has zero real benefit.
	// Might be able to clean it up on CatchExit but I don't see the point.
	void* p = HeapAlloc(GetProcessHeap(), 0, sizeof(HeapProfiler));
	heapProfiler = new(p) HeapProfiler();

	// Trawl though loaded modules and hook any mallocs, frees, reallocs and callocs we find.
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