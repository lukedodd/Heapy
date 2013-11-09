Heapy
=====

Heapy is a very simple heap profiler (or memory profiler) for windows applications.

It lets you see what parts of an application are allocating the most memory.

Heapy supports 32 and 64 bit applications written in C/C++. You do not need to modify your application in any way to use Heapy.

Heapy will hook and profile any `malloc` and `free` functions it can find. This will in turn cause `new` and `delete` to be profiled too (at least on MSVC `new` and `delete` call `malloc` and `free`.)

Build
-----

Simply clone this repository and build the `Heapy.sln` in Visual Studio 2012. More recent versions of visual studio should work, older versions will not.

Be sure to select the correct configuration for your needs: a release Win32 or x64 configuration depending on whether you want to profile 32 or 64 bit applications.


Usage
-----

Once Heapy is built the executables are put into the Release directory. To profile an application simply run Heapy.exe with the first argument as the path to the exe you wish to profile. Make sure that the debug database (`.pdb` file) is in the same directory as your target application so that you get symbol information in your stack traces.

```
Heapy_x64.exe C:\Windows\SysWOW64\notepad.exe
```

By default Heapy will run the given executable from the same folder as that executable. You can specify a working directory with an optional second argument:

```
Heapy_x64.exe C:\Windows\SysWOW64\notepad.exe C:\A\Working\Dir
```

Remember to call `Heapy_x64.exe` to profile 64 bit applications and `Heapy_Win32.exe` to profile 32 bit applications. 

Results
-------

Once your application is running Heapy will start writing profiling results to the `Heapy_Profile.txt` file in the applications working directory.

Every 10 seconds and on the termination of your program information will be added to the report.

Currently the report is very simple. Allocations are collated on a per stack trace basis. Each time we add information to the report we simply write out the top 25 allocating stack traces and the amount of memory they allocated.

Note that Heapy always *appends* to a report. You will have to delete/rename Heapy_Profile.txt or just scroll to the bottom when repeatedly profiling. 

Example
-------

This tiny test program:

```C++
#include <windows.h>

void LeakyFunction(){
	malloc(1024*1024*5); // leak 5Mb
}

void NonLeakyFunction(){
	auto p = malloc(1024*1024); // allocate 1Mb
	Sleep(15000);
	free(p); // free the Mb
}

int main()
{
	for(int i = 0; i < 5; ++i){
		LeakyFunction();
	}
	NonLeakyFunction();
	return 0;
}

```

Gave the following two reports after being run with heapy:

```
=======================================

Printing top 25 allocation points.

Alloc size     1Mb, stack trace: 
     mallocHook<0>    (0FFB0719)
     NonLeakyFunction    (00C0CF18)
     main    (00C0C942)
     __tmainCRTStartup    (00C0C87A)
     mainCRTStartup    (00C0CA5D)
     BaseThreadInitThunk    (75C6336A)
     RtlInitializeExceptionChain    (77089F72)
     RtlInitializeExceptionChain    (77089F45)

Alloc size    25Mb, stack trace: 
     mallocHook<0>    (0FFB0719)
     LeakyFunction    (00C0C448)
     main    (00C0C93B)
     __tmainCRTStartup    (00C0C87A)
     mainCRTStartup    (00C0CA5D)
     BaseThreadInitThunk    (75C6336A)
     RtlInitializeExceptionChain    (77089F72)
     RtlInitializeExceptionChain    (77089F45)

Top 7 allocations: 26Mb
Total allocations: 26Mb (difference between printed and top 7 allocations : 0Mb)

=======================================

Printing top 25 allocation points.

Alloc size    25Mb, stack trace: 
     mallocHook<0>    (0FFB0719)
     LeakyFunction    (00C0C448)
     main    (00C0C93B)
     __tmainCRTStartup    (00C0C87A)
     mainCRTStartup    (00C0CA5D)
     BaseThreadInitThunk    (75C6336A)
     RtlInitializeExceptionChain    (77089F72)
     RtlInitializeExceptionChain    (77089F45)

Top 7 allocations: 25Mb
Total allocations: 25Mb (difference between printed and top 7 allocations : 0Mb)

```

The first allocation report shows stack traces for both the leaky and non leaky alloc - it was taken before the non leaky alloc was freed so shows that 1Mb as in use. Note that the LeakyFunction allocation size was taken as the sum of all the calls to it from the loop. Also note that the LeakyFuncion alloc is the only allocation shown by the final report (which is generated on application exit) since these mallocs were never cleaned up!

How It Works
-----------

Soon I will write up a blog post (at lukedodd.com) describing how Heapy is implemented. For now hopefully the code will suffice.
