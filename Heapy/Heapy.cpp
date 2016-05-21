#include <Windows.h>
#include <Psapi.h>
#include <winternl.h>

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

typedef NTSTATUS (NTAPI *pfnNtQueryInformationProcess)(
	IN  HANDLE ProcessHandle,
	IN  PROCESSINFOCLASS ProcessInformationClass,
	OUT PVOID ProcessInformation,
	IN  ULONG ProcessInformationLength,
	OUT PULONG ReturnLength    OPTIONAL
	);

void* GetEntryPointAddress(HANDLE hProcess){
	HMODULE hModule = GetModuleHandleA("ntdll.dll");
	pfnNtQueryInformationProcess pNtQueryInformationProcess
		= (pfnNtQueryInformationProcess)GetProcAddress(hModule, "NtQueryInformationProcess");

	if (pNtQueryInformationProcess != NULL){
		PROCESS_BASIC_INFORMATION pbi;
		memset(&pbi, 0, sizeof(pbi));

		NTSTATUS status = pNtQueryInformationProcess(hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), NULL);
		if (NT_SUCCESS(status)){
			PEB* pPeb = pbi.PebBaseAddress;

			void* pImageBaseAddress;
			SIZE_T NumOfBytesRead;
			if (ReadProcessMemory(hProcess,
								  &pPeb->Reserved3[1],
								  &pImageBaseAddress,
								  sizeof(pImageBaseAddress),
								  &NumOfBytesRead) == 0
				|| NumOfBytesRead != sizeof(pImageBaseAddress))
				return NULL;

			LONG e_lfanew;
			if (ReadProcessMemory(hProcess,
								  (char*)pImageBaseAddress + offsetof(IMAGE_DOS_HEADER, e_lfanew),
								  &e_lfanew,
								  sizeof(e_lfanew),
								  &NumOfBytesRead) == 0
				|| NumOfBytesRead != sizeof(e_lfanew))
				return NULL;

			IMAGE_NT_HEADERS* pImageHeaders = (IMAGE_NT_HEADERS*)((char*)pImageBaseAddress + e_lfanew);

			DWORD EntryPointOffset;
			if (ReadProcessMemory(hProcess,
								  (char*)pImageHeaders + offsetof(IMAGE_NT_HEADERS, OptionalHeader.AddressOfEntryPoint),
								  &EntryPointOffset,
								  sizeof(EntryPointOffset),
								  &NumOfBytesRead) == 0
				|| NumOfBytesRead != sizeof(EntryPointOffset))
				return NULL;

			void* pEntryPointAddress = (char*)pImageBaseAddress + EntryPointOffset;
			return pEntryPointAddress;
		}
	}
	return NULL;
}

bool PatchEntryPoint(HANDLE hProcess, void* pEntryPointAddress, unsigned char* pWriteBytes, unsigned char* pOriginalBytes, unsigned int unSize){
	SIZE_T NumOfBytesRead;
	if (pOriginalBytes != NULL){
		if (ReadProcessMemory(hProcess, pEntryPointAddress, pOriginalBytes, unSize, &NumOfBytesRead) == 0
			|| NumOfBytesRead != unSize)
		{
			return false;
		}
	}
	if (WriteProcessMemory(hProcess, pEntryPointAddress, pWriteBytes, unSize, &NumOfBytesRead) == 0
		|| NumOfBytesRead != unSize){
		return false;
	}

	FlushInstructionCache(hProcess, pEntryPointAddress, unSize);
	return true;
}

struct ProcessStartContext{
	void* pEntryPointAddress;
	unsigned char OldOpCodes[2];
};

bool WaitForProcessStart(HANDLE hProcess, HANDLE hThread, ProcessStartContext* pProcessContext){
	void* pEntryPointAddress = GetEntryPointAddress(hProcess);
	if (pEntryPointAddress == NULL)
		return false;

	unsigned char OldBytes[2];
	unsigned char NewBytes[2] = { 0xEB, 0xFE };
	if (!PatchEntryPoint(hProcess, pEntryPointAddress, NewBytes, OldBytes, 2))
		return false;

	pProcessContext->pEntryPointAddress = pEntryPointAddress;
	memcpy(pProcessContext->OldOpCodes, OldBytes, 2);

	ResumeThread(hThread);

	CONTEXT context;
	memset(&context, 0, sizeof(context));
#ifdef _WIN64
	for (unsigned int i = 0; i < 50 && context.Rip != (decltype(context.Rip))pEntryPointAddress; ++i){
#else
	for (unsigned int i = 0; i < 50 && context.Eip != (decltype(context.Eip))pEntryPointAddress; ++i){
#endif
		// patience.
		Sleep(100);
 
		// read the thread context
		context.ContextFlags = CONTEXT_CONTROL;
		GetThreadContext(hThread, &context);
	}

	return true;
}

bool ResumeProcessStart(HANDLE hProcess, HANDLE hThread, ProcessStartContext* pProcessContext){
	SuspendThread(hThread);
	if (!PatchEntryPoint(hProcess, pProcessContext->pEntryPointAddress, pProcessContext->OldOpCodes, NULL, 2))
		return false;

	ResumeThread(hThread);
	return true;
}

// Inject a DLL into the target process by creating a new thread at LoadLibrary
// Waits for injected thread to finish and returns its exit code.
// 
// Originally from :
// http://www.codeproject.com/Articles/2082/API-hooking-revealed 
DWORD LoadLibraryInjection(HANDLE proc, const char *dllName){
	LPVOID RemoteString, LoadLibAddy;
	LoadLibAddy = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");

	RemoteString = (LPVOID)VirtualAllocEx(proc, NULL, strlen(dllName), MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
	if(RemoteString == NULL){
		CloseHandle(proc); // Close the process handle.
		throw std::runtime_error("LoadLibraryInjection: Error on VirtualAllocEx.");
	}

	if(WriteProcessMemory(proc, (LPVOID)RemoteString, dllName,strlen(dllName), NULL) == 0){
		VirtualFreeEx(proc, RemoteString, 0, MEM_RELEASE); // Free the memory we were going to use.
		CloseHandle(proc); // Close the process handle.
		throw std::runtime_error("LoadLibraryInjection: Error on WriteProcessMemeory.");
	}

	HANDLE hThread;

	if((hThread = CreateRemoteThread(proc, NULL, NULL, (LPTHREAD_START_ROUTINE)LoadLibAddy, (LPVOID)RemoteString, NULL, NULL)) == NULL){
		VirtualFreeEx(proc, RemoteString, 0, MEM_RELEASE); // Free the memory we were going to use.
		CloseHandle(proc); // Close the process handle.
		throw std::runtime_error("LoadLibraryInjection: Error on CreateRemoteThread.");
	}

	// Wait for the thread to finish.
	WaitForSingleObject(hThread, INFINITE);

	// Lets see what it says...
	DWORD dwThreadExitCode=0;
	GetExitCodeThread(hThread,  &dwThreadExitCode);

	// No need for this handle anymore, lets get rid of it.
	CloseHandle(hThread);

	// Lets clear up that memory we allocated earlier.
	VirtualFreeEx(proc, RemoteString, 0, MEM_RELEASE);

	return dwThreadExitCode;
}

std::string getDirectoryOfFile(const std::string &file){
	size_t pos = (std::min)(file.find_last_of("/"), file.find_last_of("\\"));
	if(pos == std::string::npos)
		return ".";
	else
		return file.substr(0, pos);
}

extern "C" int main(int argc, char* argv[]){
	if(argc < 2){
		std::cout << "No exe specified!\n\n";
		std::cout << "Usage: Heapy <exe path> [args to pass to exe]\n\n"
					 "       The first argument specifies the exe to launch.\n"
					 "       Subsequent arguments are passed to launched exe.\n";
			 
		return -1;
	}
	char *injectionTarget = argv[1];

	bool win64 = false;
	#ifdef _WIN64
		win64 = true;
	#endif
	// Select correct dll name depending on whether x64 or win32 version launched.
	std::string heapyInjectDllName;
	if(win64)
		heapyInjectDllName = "HeapyInject_x64.dll";
	else
		heapyInjectDllName = "HeapyInject_Win32.dll";

	// Assume that the injection payload dll is in the same directory as the exe.
	CHAR exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH );
	std::string dllPath = getDirectoryOfFile(std::string(exePath)) + "\\" + heapyInjectDllName;

	std::string commandLine = injectionTarget;
	for(int i = 2; i < argc; ++i){
		commandLine += " " + std::string(argv[i]);
	}

	// Start our new process with a suspended main thread.
	std::cout << "Starting process with heap profiling enabled..." << std::endl;
	std::cout << "Target exe path: " << injectionTarget << std::endl;
	std::cout << "Target execommand line: " << commandLine;
	std::cout << "Dll to inject: " << dllPath << std::endl;


	DWORD flags = CREATE_SUSPENDED;
	PROCESS_INFORMATION pi;
	STARTUPINFOA si;
	GetStartupInfoA(&si);

	// CreatePRocessA can modify input arg so do this to be safe.
	std::vector<char> commandLineMutable(commandLine.begin(), commandLine.end()); 

	if(CreateProcessA(NULL, commandLineMutable.data(), NULL, NULL, 0, flags, NULL, 
		             (LPSTR)".", &si, &pi) == 0){
		std::cerr << "Error creating process " << injectionTarget << std::endl;
		return -1;
	}

	ProcessStartContext ProcessContext;
	if (!WaitForProcessStart(pi.hProcess, pi.hThread, &ProcessContext))
		return -1;

	// Inject our dll.
	// This method returns only when injection thread returns.
	try{
		if(!LoadLibraryInjection(pi.hProcess, dllPath.c_str())){
			throw std::runtime_error("LoadLibrary failed!");
		}
	}catch(const std::exception &e){
		std::cerr << "\n";
		std::cerr << "Error while injecting process: " << e.what() << "\n\n";
		std::cerr << "Check that the hook dll (" << dllPath << " is in the correct location.\n\n";
		std::cerr << "Are you trying to inject a " << (win64 ? " 32 bit " : " 64 bit ") << " application using the "
			<<  (win64 ? " 64 bit " : " 32 bit ") << " injector?\n\n";

		// TODO: figure out how to terminate thread. This does not always work.
		TerminateProcess(pi.hProcess, 0);
		return -1;
	}
	
	// Once the injection thread has returned it is safe to resume the main thread.
	ResumeProcessStart(pi.hProcess, pi.hThread, &ProcessContext);

	// Wait for the target application to exit. 
	// This doesn't matter to much, but makes heapy nicer to use in test scripts.
	// (Like the ProfileTestApplication project.)
	WaitForSingleObject(pi.hProcess, INFINITE);
	return 0;
}
