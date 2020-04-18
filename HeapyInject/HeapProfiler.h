#pragma once
#include <ostream>
#include <vector>
#include <unordered_map>
#include <set>
#include <Windows.h>

const int backtraceSize = 62;
typedef size_t StackHash;

struct StackTrace{
	void *backtrace[backtraceSize];
	StackHash hash;

	StackTrace();
	void trace(); 
	void print(std::ostream &stream) const;
};


// We define our own Mutex type since we can't use any standard library features inside parts of heapy.
struct Mutex{
	CRITICAL_SECTION criticalSection;
	Mutex(){
		InitializeCriticalSectionAndSpinCount(&criticalSection, 400);
	}
	~Mutex(){
		DeleteCriticalSection(&criticalSection);
	}
};

struct lock_guard{
	Mutex& mutex;
	lock_guard(Mutex& mutex) : mutex(mutex){
		EnterCriticalSection(&mutex.criticalSection);
	}
	~lock_guard(){
		LeaveCriticalSection(&mutex.criticalSection);
	}
};

template<class _Ty>
struct HeapAllocator
{
public:
	typedef _Ty value_type;
#if _MSC_VER <= 1700
	typedef value_type* pointer;
	typedef value_type& reference;
	typedef const value_type* const_pointer;
	typedef const value_type& const_reference;

	typedef size_t size_type;
	typedef ptrdiff_t difference_type;

	template<class _Other>
		struct rebind
		{	// convert this type to _ALLOCATOR<_Other>
		typedef HeapAllocator<_Other> other;
		};

	pointer address(reference _Val) const
		{	// return address of mutable _Val
		return ((pointer) &(char&)_Val);
		}

	const_pointer address(const_reference _Val) const
		{	// return address of nonmutable _Val
		return ((const_pointer) &(char&)_Val);
		}
#endif
	HeapAllocator() _THROW0()
		{	// construct default allocator (do nothing)
		}
#if _MSC_VER <= 1700
	HeapAllocator(const HeapAllocator<_Ty>&) _THROW0()
		{	// construct by copying (do nothing)
		}
#endif
	template<class _Other>
		HeapAllocator(const HeapAllocator<_Other>&) _THROW0()
		{	// construct from a related allocator (do nothing)
		}
#if _MSC_VER <= 1700
	template<class _Other>
		HeapAllocator<_Ty>& operator=(const HeapAllocator<_Other>&)
		{	// assign from a related allocator (do nothing)
		return (*this);
		}
#endif
	void deallocate(value_type* _Ptr, size_t)
		{	// deallocate object at _Ptr, ignore size
			HeapFree(GetProcessHeap(), 0, _Ptr);
		}

	value_type* allocate(size_t _Count)
		{	// allocate array of _Count elements
		void *_Ptr = 0;

		if (_Count <= 0)
			_Count = 0;
		else if ((size_t)(-1) / sizeof (_Ty) < _Count)
			throw std::bad_array_new_length();
		else if ((_Ptr = HeapAlloc(GetProcessHeap(), 0, _Count * sizeof (_Ty))) == 0)
			throw std::bad_alloc();

		return ((_Ty*)_Ptr);
		}
#if _MSC_VER <= 1700
	pointer allocate(size_type _Count, const void*)
		{	// allocate array of _Count elements, ignore hint
		return (allocate(_Count));
		}

	void construct(pointer _Ptr, const _Ty& _Val)
		{	// construct object at _Ptr with value _Val
		_Construct(_Ptr, _Val);
		}

	void construct(pointer _Ptr, _Ty&& _Val)
		{	// construct object at _Ptr with value _Val
		::new ((void*)_Ptr) _Ty(std::forward<_Ty>(_Val));
		}

	template<class _Other>
		void construct(pointer _Ptr, _Other&& _Val)
		{	// construct object at _Ptr with value _Val
		::new ((void*)_Ptr) _Ty(std::forward<_Other>(_Val));
		}

	void destroy(pointer _Ptr)
		{	// destroy object at _Ptr
		_Destroy(_Ptr);
		}

	size_t max_size() const _THROW0()
		{	// estimate maximum array size
		size_t _Count = (size_t)(-1) / sizeof (_Ty);
		return (0 < _Count ? _Count : 1);
		}
#endif
};

template<class _Ty,
	class _Other> inline
	bool operator==(const HeapAllocator<_Ty>&,
		const HeapAllocator<_Other>&) _THROW0()
	{	// test for allocator equality
	return (true);
	}

template<class _Ty,
	class _Other> inline
	bool operator!=(const HeapAllocator<_Ty>& _Left,
		const HeapAllocator<_Other>& _Right) _THROW0()
	{	// test for allocator inequality
	return (!(_Left == _Right));
	}

class HeapProfiler{
public:
	HeapProfiler();
	~HeapProfiler();

	void malloc(void *ptr, size_t size, const StackTrace &trace);
	void free(void *ptr, const StackTrace &trace);

	struct CallStackInfo {
		StackTrace trace;
		size_t totalSize;
		size_t n;
	};

	// Return a list of allocation sites (a particular stack trace) and the amount
	// of memory currently allocated by each site.
	void getAllocationSiteReport(std::vector<CallStackInfo> &allocs);
private:
	Mutex mutex;

	struct PointerInfo {
		StackHash stack;
		size_t size;
	};
	typedef std::unordered_map<StackHash, CallStackInfo, std::hash<StackHash>, std::equal_to<StackHash>, HeapAllocator<std::pair<const StackHash, CallStackInfo> > > StackTraceCollection_t;
	StackTraceCollection_t stackTraces;
	typedef std::unordered_map<void*, PointerInfo, std::hash<void*>, std::equal_to<void*>, HeapAllocator<std::pair<const void*, PointerInfo> > > PtrCollection_t;
	PtrCollection_t ptrs;
};
