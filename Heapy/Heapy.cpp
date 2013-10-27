#include <iostream>
#include "EasyHook.h"
#include <stdio.h>
#include <conio.h>
#include <thread>
#include <vector>

#define FORCE(expr)     {if(!SUCCEEDED(NtStatus = (expr))) goto ERROR_ABORT;}


std::wstring get_wstring(const std::string & s)
{
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
	NTSTATUS                NtStatus;
	ULONG pid = 0;
	printf("%s",argv[1]);
	auto wstring = get_wstring(argv[1]);
	FORCE(RhCreateAndInject((wchar_t *)wstring.c_str(), L"", 0, EASYHOOK_INJECT_DEFAULT, L"", L"Z:/Heapy/RelWithDebInfo/HeapyInjectDll.dll", 0, NULL, &pid));

	return 0;


	ERROR_ABORT:

	printf("\n[Error(0x%p)]: \"%S\" (code: %d {0x%p})\n", (PVOID)NtStatus, RtlGetLastErrorString(), RtlGetLastError(), (PVOID)RtlGetLastError());

    return NtStatus;
}

