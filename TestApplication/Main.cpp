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
