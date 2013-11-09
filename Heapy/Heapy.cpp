#include <Windows.h>
#include <Psapi.h>
#include <strsafe.h>

#include <iostream>
#include <stdio.h>
#include <conio.h>
#include <thread>
#include <vector>
#include <string>

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
	return file.substr(0, pos);
}

extern "C" int main(int argc, char* argv[]){
	if(argc < 2){
		std::cout << "No exe specified!\n\n";
		std::cout << "Usage: Heapy <exe path> [target working dir]\n\n"
		             "       The first argument specifies the exe to launch.\n"
		             "       The second optional argument specifies the working directory\n"
		             "       of the exe to launch (this defaults to directory of exe.)\n";
			 
		return -1;
	}
	char *injectionTarget = argv[1];
	std::string injectionTargetWorkingDirectory;

	if(argc > 2)
		injectionTargetWorkingDirectory = argv[2];
	else
		injectionTargetWorkingDirectory = getDirectoryOfFile(injectionTarget);


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
	std::string exePath(argv[0]);
	std::string dllPath = getDirectoryOfFile(exePath) + "\\" + heapyInjectDllName;

	// Start our new process with a suspended main thread.
	std::cout << "Starting process with heap profiling enabled..." << std::endl;
	std::cout << "Exe path: " << exePath << std::endl;
	std::cout << "Working directory: " << injectionTargetWorkingDirectory << std::endl;
	std::cout << "Dll to inject: " << dllPath << std::endl;

	DWORD flags = CREATE_SUSPENDED;
	PROCESS_INFORMATION pi;
	STARTUPINFOA si;
    GetStartupInfoA(&si);
	if(CreateProcessA(NULL, injectionTarget, NULL, NULL, 0, flags, NULL, 
		             (LPSTR)injectionTargetWorkingDirectory.c_str(), &si, &pi) == 0){
		std::cerr << "Error creating process " << injectionTarget << " with working directory " << injectionTargetWorkingDirectory << std::endl;
		return -1;
	}
		
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
	ResumeThread(pi.hThread);
	return 0;
}
