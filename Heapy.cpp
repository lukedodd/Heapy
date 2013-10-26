#include <iostream>
#include "EasyHook.h"
#include <stdio.h>
#include <conio.h>
#include <thread>
#include <vector>

#define FORCE(expr)     {if(!SUCCEEDED(NtStatus = (expr))) goto ERROR_ABORT;}

extern "C" int main(int argc, wchar_t* argv[])
{
	NTSTATUS                NtStatus;
	ULONG pid = 0;
	FORCE(RhCreateAndInject(L"X:/TestStatic/TestStatic/x64/Release/TestStatic.exe", L"", 0, EASYHOOK_INJECT_DEFAULT, L"", L"Z:/Heapy/RelWithDebInfo/HeapyInjectDll.dll", 0, NULL, &pid));

	Sleep(1000);
	return 0;


	ERROR_ABORT:

	printf("\n[Error(0x%p)]: \"%S\" (code: %d {0x%p})\n", (PVOID)NtStatus, RtlGetLastErrorString(), RtlGetLastError(), (PVOID)RtlGetLastError());

    _getch();

    return NtStatus;
}

