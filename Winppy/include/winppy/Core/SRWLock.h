#pragma once

#include <winppy/Platform/Platform.h>

namespace winppy
{
	class SRWLockExclusiveGuard
	{
	public:
		SRWLockExclusiveGuard(SRWLOCK& lock)
			: m_lock(lock)
		{
			AcquireSRWLockExclusive(&m_lock);
		}
		SRWLockExclusiveGuard(SRWLOCK* pLock)
			: m_lock(*pLock)
		{
			AcquireSRWLockExclusive(&m_lock);
		}
		~SRWLockExclusiveGuard()
		{
			ReleaseSRWLockExclusive(&m_lock);
		}
	private:
		SRWLOCK& m_lock;
	};

	class SRWLockSharedGuard
	{
	public:
		SRWLockSharedGuard(SRWLOCK& lock)
			: m_lock(lock)
		{
			AcquireSRWLockShared(&m_lock);
		}
		SRWLockSharedGuard(SRWLOCK* pLock)
			: m_lock(*pLock)
		{
			AcquireSRWLockShared(&m_lock);
		}
		~SRWLockSharedGuard()
		{
			ReleaseSRWLockShared(&m_lock);
		}
	private:
		SRWLOCK& m_lock;
	};
}
