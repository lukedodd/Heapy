#pragma once
#include <ostream>
#include <vector>
#include <unordered_map>
#include <set>
#include <Windows.h>

const int backtraceSize = 64;
typedef unsigned long StackHash;

struct StackTrace{
	void *backtrace[backtraceSize];
	StackHash hash;

	StackTrace();
	void trace(); 
	void print(std::ostream &stream) const;
};

template<class _Ty>
	struct HeapAllocator_base
	{	// base class for generic allocators
	typedef _Ty value_type;
	};

		// TEMPLATE CLASS _Allocator_base<const _Ty>
template<class _Ty>
	struct HeapAllocator_base<const _Ty>
	{	// base class for generic allocators for const _Ty
	typedef _Ty value_type;
	};

template<class _Ty>
struct HeapAllocator : HeapAllocator_base<_Ty>
{
public:
	typedef HeapAllocator_base<_Ty> _Mybase;
	typedef typename _Mybase::value_type value_type;

	typedef value_type _FARQ *pointer;
	typedef value_type _FARQ& reference;
	typedef const value_type _FARQ *const_pointer;
	typedef const value_type _FARQ& const_reference;

	typedef _SIZT size_type;
	typedef _PDFT difference_type;

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

	HeapAllocator() _THROW0()
		{	// construct default allocator (do nothing)
		}

	HeapAllocator(const HeapAllocator<_Ty>&) _THROW0()
		{	// construct by copying (do nothing)
		}

	template<class _Other>
		HeapAllocator(const HeapAllocator<_Other>&) _THROW0()
		{	// construct from a related allocator (do nothing)
		}

	template<class _Other>
		HeapAllocator<_Ty>& operator=(const HeapAllocator<_Other>&)
		{	// assign from a related allocator (do nothing)
		return (*this);
		}

	void deallocate(pointer _Ptr, size_type)
		{	// deallocate object at _Ptr, ignore size
		//::operator delete(_Ptr);
			HeapFree(GetProcessHeap(), 0, _Ptr);
		}

	pointer allocate(size_type _Count)
		{	// allocate array of _Count elements
		//return (_Allocate(_Count, (pointer)0));
		void *_Ptr = 0;

		if (_Count <= 0)
			_Count = 0;
		else if (((_SIZT)(-1) / sizeof (_Ty) < _Count)
			|| (_Ptr = HeapAlloc(GetProcessHeap(), 0, _Count * sizeof (_Ty))) == 0)
			_THROW_NCEE(std::bad_alloc, 0);

		return ((_Ty _FARQ *)_Ptr);
		}

	pointer allocate(size_type _Count, const void _FARQ *)
		{	// allocate array of _Count elements, ignore hint
		return (allocate(_Count));
		}

	void construct(pointer _Ptr, const _Ty& _Val)
		{	// construct object at _Ptr with value _Val
		_Construct(_Ptr, _Val);
		}

	void construct(pointer _Ptr, _Ty&& _Val)
		{	// construct object at _Ptr with value _Val
		::new ((void _FARQ *)_Ptr) _Ty(_STD forward<_Ty>(_Val));
		}

	template<class _Other>
		void construct(pointer _Ptr, _Other&& _Val)
		{	// construct object at _Ptr with value _Val
		::new ((void _FARQ *)_Ptr) _Ty(_STD forward<_Other>(_Val));
		}

	void destroy(pointer _Ptr)
		{	// destroy object at _Ptr
		_Destroy(_Ptr);
		}

	_SIZT max_size() const _THROW0()
		{	// estimate maximum array size
		_SIZT _Count = (_SIZT)(-1) / sizeof (_Ty);
		return (0 < _Count ? _Count : 1);
		}
};

template<> class HeapAllocator<void>
	{	// generic _ALLOCATOR for type void
public:
	typedef void _Ty;
	typedef _Ty _FARQ *pointer;
	typedef const _Ty _FARQ *const_pointer;
	typedef _Ty value_type;

	template<class _Other>
		struct rebind
		{	// convert this type to an _ALLOCATOR<_Other>
		typedef HeapAllocator<_Other> other;
		};

	HeapAllocator() _THROW0()
		{	// construct default allocator (do nothing)
		}

	HeapAllocator(const HeapAllocator<_Ty>&) _THROW0()
		{	// construct by copying (do nothing)
		}

	template<class _Other>
		HeapAllocator(const HeapAllocator<_Other>&) _THROW0()
		{	// construct from related allocator (do nothing)
		}

	template<class _Other>
		HeapAllocator<_Ty>& operator=(const HeapAllocator<_Other>&)
		{	// assign from a related allocator (do nothing)
		return (*this);
		}
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

	// Return a list of allocation sites (a particular stack trace) and the amount
	// of memory currently allocated by each site.
	void getAllocationSiteReport(std::vector<std::pair<StackTrace, size_t>> &allocs);
private:
	HANDLE mutex;
	typedef std::unordered_map<void *, size_t, std::hash<void*>, std::equal_to<void*>, HeapAllocator<std::pair<const void*, size_t> > > TraceInfoAllocCollection_t;
	struct TraceInfo{
		StackTrace trace;
		TraceInfoAllocCollection_t allocations;
	};
	typedef std::unordered_map<StackHash, TraceInfo, std::hash<StackHash>, std::equal_to<StackHash>, HeapAllocator<std::pair<const StackHash, TraceInfo> > > StackTraceCollection_t;
	StackTraceCollection_t stackTraces;
	typedef std::unordered_map<void*, StackHash, std::hash<void*>, std::equal_to<void*>, HeapAllocator<std::pair<const void*, StackHash> > > PtrCollection_t;
	PtrCollection_t ptrs;
};
