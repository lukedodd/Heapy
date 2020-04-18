#include <windows.h>
#include <iostream>

void MallocLeakyFunction(){
	malloc(1024*1024*5); // leak 5Mb
}

void MallocNonLeakyFunction(){
	auto p = malloc(1024*1024); // allocate 1Mb
	std::cout << "TestApplication: Sleeping..." << std::endl;
	Sleep(15000);
	free(p); // free the Mb
}

void CallocLeakyFunction(){
	calloc(1024, 1024*5); // leak 5Mb
}

void CallocNonLeakyFunction(){
	auto p = calloc(1024, 1024); // allocate 1Mb
	std::cout << "TestApplication: Sleeping..." << std::endl;
	Sleep(15000);
	free(p); // free the Mb
}

void ReallocLeakyFunction(){
	auto p = realloc(NULL, 1024*1024*1); // allocate 1Mb
	auto p2 = realloc(p, 1024*1024*5); // reallocate and leak 5Mb
}

void ReallocNonLeakyFunction(){
	auto p = realloc(NULL, 1024*1024); // allocate 1Mb
	std::cout << "TestApplication: Sleeping..." << std::endl;
	Sleep(15000);
	auto p2 = realloc(p, 1024*1024*2); // reallocate 2Mb
	if (p2 != NULL)
		p = p2;
	Sleep(15000);
	realloc(p, 0); // free the Mb (as specified by CRT reference)
}

int main()
{
	std::cout << "TestApplication: Creating some leaks..." << std::endl;
	for(int i = 0; i < 5; ++i){
		MallocLeakyFunction();
		CallocLeakyFunction();
		ReallocLeakyFunction();
	}
	MallocNonLeakyFunction();
	CallocNonLeakyFunction();
	ReallocNonLeakyFunction();
	std::cout << "TestApplication: Exiting..." << std::endl;
	return 0;
}
