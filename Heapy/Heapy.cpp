#include <iostream>
#include "EasyHook.h"
#include <stdio.h>
#include <conio.h>
#include <thread>
#include <vector>

#pragma warning(disable: 4996)

std::wstring getWstring(const std::string & s){
  const char * cs = s.c_str();
  const size_t wn = mbsrtowcs(NULL, &cs, 0, NULL);

  if (wn == size_t(-1))
  {
    std::cout << "Error in mbsrtowcs(): " << errno << std::endl;
    return L"";
  }

  std::vector<wchar_t> buf(wn + 1);
  const size_t wn_again = mbsrtowcs(&buf[0], &cs, wn + 1, NULL);

  if (wn_again == size_t(-1))
  {
    std::cout << "Error in mbsrtowcs(): " << errno << std::endl;
    return L"";
  }

  return std::wstring(&buf[0], wn);
}

extern "C" int main(int argc, char* argv[])
{
	auto wstring = getWstring(argv[1]);
	std::string exePath(argv[0]);
	// Assume that the injection payload dll is in the same directory as the exe.
	std::string dllPath = exePath.append(std::string("/../HeapyInjectDll.dll"));

	ULONG pid = 0;
	NTSTATUS status = RhCreateAndInject((wchar_t *)wstring.c_str(), 
		                                L"", 
										0, 
										EASYHOOK_INJECT_DEFAULT,
										L"", 
										(wchar_t *)getWstring(dllPath.c_str()).c_str(),
										0, 
										NULL, 
										&pid);

	if(!SUCCEEDED(status)){
		printf("\nError while creating and injecting process: (0x%p) \"%S\" (code: %d {0x%p})\n", (PVOID)status, RtlGetLastErrorString(), RtlGetLastError(), (PVOID)RtlGetLastError());
		return status;
	}

	return 0;
}

