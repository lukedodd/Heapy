Heapy
=====

Heapy, a very simple heap profiler (or memory profiler), supports 32 and 64 bit windows applications written in C/C++ without modifying your application. 

It lets you see what parts of an application are allocating the most memory.

Heapy will hook and profile any `malloc`, `realloc`, `calloc` and `free` functions it can find, which will in turn cause `new` and `delete` to be profiled too (at least on MSVC `new` and `delete` call `malloc` and `free`).

Download
--------

You can download the latest Luke Dodd's offical release of Heapy at [here.](https://github.com/lukedodd/Heapy/releases)


Build
-----

Simply clone this repository and build the `Heapy.sln` in Visual Studio 2010. More recent versions of visual studio should work, older versions will not.

Be sure to select the correct configuration for your needs: a release Win32 or x64 configuration depending on whether you want to profile 32 or 64 bit applications.


Usage
-----

Once Heapy is built the executables are put into the Release directory. To profile an application simply run Heapy.exe with the first argument as the path to the exe you wish to profile. Subsequent arguments are passed to the target application. Make sure that the debug database (`.pdb` file) is in the same directory as your target application so that you get symbol information in your stack traces. You can profile release builds but profiling debug or unoptimised builds gives the nicest stack traces.

```
Heapy_x64.exe C:\Windows\System32\notepad.exe test.txt
```

The above examples assumes you have 64 bit windows. Remember to call `Heapy_x64.exe` to profile 64 bit applications and `Heapy_Win32.exe` to profile 32 bit applications. 

Results
-------

Once your application is running Heapy will start writing profiling results to the `Heapy_Profile.txt` file in the applications working directory.

Every 10 seconds and on the termination of your program information will be added to the report.

Currently the report is very simple. Allocations are collated on a per stack trace basis. Each time we add information to the report we simply write out the top 25 allocating stack traces and the amount of memory they allocated.

Note that Heapy always *appends* to a report. You will have to delete/rename `Heapy_Profile.txt` or just scroll to the bottom when repeatedly profiling. 

Example
-------

This tiny test program `TestApplication`:

```C++
#include <windows.h>
#include <iostream>

void LeakyFunction(){
	malloc(1024*1024*5); // leak 5Mb
}

void NonLeakyFunction(){
	auto p = malloc(1024*1024); // allocate 1Mb
	std::cout << "TestApplication: Sleeping..." << std::endl;
	Sleep(15000);
	free(p); // free the Mb
}

int main()
{
	std::cout << "TestApplication: Creating some leaks..." << std::endl;
	for(int i = 0; i < 5; ++i){
		LeakyFunction();
	}
	NonLeakyFunction();
	std::cout << "TestApplication: Exiting..." << std::endl;
	return 0;
}
```

Gave the following two reports in `Heapy_Profile.txt` after being run with heapy:

```
=======================================

Printing top allocation points.

< Trimmed out very small allocations from std::streams >

Alloc size 1Mb, stack trace: 
    NonLeakyFunction    e:\sourcedirectory\heapy\testapplication\main.cpp:9    (000000013FEC1D7E)
    main    e:\sourcedirectory\heapy\testapplication\main.cpp:22    (000000013FEC1E0D)
    __tmainCRTStartup    f:\dd\vctools\crt_bld\self_64_amd64\crt\src\crt0.c:241    (000000013FEC67FC)
    BaseThreadInitThunk    (00000000779A652D)
    RtlUserThreadStart    (0000000077ADC541)

Alloc size 25Mb, stack trace: 
    LeakyFunction    e:\sourcedirectory\heapy\testapplication\main.cpp:6    (000000013FEC1D5E)
    main    e:\sourcedirectory\heapy\testapplication\main.cpp:20    (000000013FEC1E06)
    __tmainCRTStartup    f:\dd\vctools\crt_bld\self_64_amd64\crt\src\crt0.c:241    (000000013FEC67FC)
    BaseThreadInitThunk    (00000000779A652D)
    RtlUserThreadStart    (0000000077ADC541)

Top 13 allocations: 26.005Mb
Total allocations: 26.005Mb (difference between total and top 13 allocations : 0Mb)

=======================================

Printing top allocation points.

< Trimmed out very small allocations from std::streams >

Alloc size 25Mb, stack trace: 
    LeakyFunction    e:\sourcedirectory\heapy\testapplication\main.cpp:6    (000000013FEC1D5E)
    main    e:\sourcedirectory\heapy\testapplication\main.cpp:20    (000000013FEC1E06)
    __tmainCRTStartup    f:\dd\vctools\crt_bld\self_64_amd64\crt\src\crt0.c:241    (000000013FEC67FC)
    BaseThreadInitThunk    (00000000779A652D)
    RtlUserThreadStart    (0000000077ADC541)

Top 5 allocations: 25.005Mb
Total allocations: 25.005Mb (difference between total and top 5 allocations : 0Mb)


```

The first allocation report shows stack traces for both the leaky and non leaky alloc - it was taken before the non leaky alloc was freed so shows that 1Mb as in use. Note that the LeakyFunction allocation size was taken as the sum of all the calls to it from the loop. Also note that the LeakyFuncion alloc is the only allocation shown by the final report (which is generated on application exit) since these mallocs were never cleaned up!

You can run Heapy on the test application above by building the `ProfileTestApplication` project in the solution (you must manually click to build that project, it's not set to build on "Build All".)

How It Works
-----------

This [blog post](http://www.lukedodd.com/heapy-heap-profiler/) describes Heapy in detail.

Future
------

Right now Heapy is pretty much a proof of concept that's useful enough to diagnose simple leaks. Visual Studio 2015 seems to have native heap profiling built in so I may be using that in future.
