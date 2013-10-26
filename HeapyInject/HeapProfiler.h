#pragma once

const int backtraceSize = 256;

struct StackTrace{
	void *backtrace[backtraceSize];
	unsigned long hash;

	StackTrace();
	void trace(); 
	void print() const;
};

void PrintStack();

class HeapProfiler{

};