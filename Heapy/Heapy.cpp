#include <Windows.h>
#include <Psapi.h>
#include <strsafe.h>

#include <iostream>
#include <stdio.h>
#include <conio.h>
#include <thread>
#include <vector>

// Print last error code. 
// Originally from: http://msdn.microsoft.com/en-us/library/windows/desktop/ms680582(v=vs.85).aspx
// I found this version in: https://code.google.com/p/injex/source/browse/trunk/src/injex/injex.cpp
void ErrorExit(LPTSTR lpszFunction, LPCSTR lpAdditionalHelp) { 
	// Retrieve the system error message for the last-error code
	LPVOID lpMsgBuf;
	LPVOID lpDisplayBuf;
	DWORD dw = GetLastError(); 

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		dw,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &lpMsgBuf,
		0, NULL );

	// Display the error message and exit the process

	lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, 
		(lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR)); 
	StringCchPrintf((LPTSTR)lpDisplayBuf, 
		LocalSize(lpDisplayBuf) / sizeof(TCHAR),
		TEXT("ERROR: %s failed with error %d: %s"), 
		lpszFunction, dw, lpMsgBuf); 

	wprintf((LPWSTR)lpDisplayBuf);
	if(lpAdditionalHelp != NULL)
		printf("ADDITIONAL HELP: %s\n",lpAdditionalHelp);

	LocalFree(lpMsgBuf);
	LocalFree(lpDisplayBuf);
	ExitProcess(dw);
}

// Inject a DLL into the target process by creating a new thread at LoadLibrary
// Originally from :
// http://www.codeproject.com/Articles/2082/API-hooking-revealed CTRL+F: "Injecting DLL by using CreateRemoteThread() API function"
// I found it in: https://code.google.com/p/injex/source/browse/trunk/src/injex/injex.cpp
DWORD LoadLibraryInjection(HANDLE proc, const char *dllName){
	LPVOID RemoteString, LoadLibAddy;
	LoadLibAddy = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");

	RemoteString = (LPVOID)VirtualAllocEx(proc, NULL, strlen(dllName), MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
	if(RemoteString == NULL){
		CloseHandle(proc); // Close the process handle.
		ErrorExit(TEXT("VirtualAllocEx"), NULL);
	}

	if(WriteProcessMemory(proc, (LPVOID)RemoteString, dllName,strlen(dllName), NULL) == 0){
		VirtualFreeEx(proc, RemoteString, 0, MEM_RELEASE); // Free the memory we were going to use.
		CloseHandle(proc); // Close the process handle.
		ErrorExit(TEXT("WriteProcessMemory"), NULL);
	}

	HANDLE hThread;

	if((hThread = CreateRemoteThread(proc, NULL, NULL, (LPTHREAD_START_ROUTINE)LoadLibAddy, (LPVOID)RemoteString, NULL, NULL)) == NULL){
		VirtualFreeEx(proc, RemoteString, 0, MEM_RELEASE); // Free the memory we were going to use.
		CloseHandle(proc); // Close the process handle.
		ErrorExit(TEXT("CreateRemoteThread"), NULL);
	}
	DWORD dwThreadExitCode=0;

	// Lets wait for the thread to finish 10 seconds is our limit.
	// During this wait, DllMain is running in the injected DLL, so
	// DllMain has 10 seconds to run.
	WaitForSingleObject(hThread, INFINITE);

	// Lets see what it says...
	GetExitCodeThread(hThread,  &dwThreadExitCode);

	// No need for this handle anymore, lets get rid of it.
	CloseHandle(hThread);

	// Lets clear up that memory we allocated earlier.
	VirtualFreeEx(proc, RemoteString, 0, MEM_RELEASE);

	return dwThreadExitCode;
}

extern "C" int main(int argc, char* argv[]){
	char *injectionTarget = argv[1];
	char *injectionTargetWorkingDirectory = argc > 2 ? argv[2] : NULL;

	// Assume that the injection payload dll is in the same directory as the exe.
	std::string exePath(argv[0]);
	std::string dllPath = exePath.append(std::string("/../HeapyInjectDll.dll"));

	// Start our new process with a suspended main thread.
	DWORD dwFlags = CREATE_SUSPENDED;
	PROCESS_INFORMATION pi;
	STARTUPINFOA si;
    GetStartupInfoA(&si);
	printf("Starting new process to inject into:\n%s\n", injectionTarget);
	if(CreateProcessA(NULL, injectionTarget, NULL, NULL, 0, dwFlags, NULL, injectionTargetWorkingDirectory, &si, &pi) == 0)
		ErrorExit(TEXT("CreateProcessA"), "Check your process path.");

	// Inject our dll.
	// This method returns only when injection thread returns.
	LoadLibraryInjection(pi.hProcess, dllPath.c_str());
	
	// Once the injection thread has returned it is safe to resume the main thread.
	ResumeThread(pi.hThread);
	return 0;
}

