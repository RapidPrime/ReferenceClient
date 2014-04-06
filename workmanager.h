#pragma once
#include <memory>
#include <vector>
#include <boost/asio.hpp>
#include "bignum.h"
#include "packet.h"
#include "tcpconnection.h"


struct ServerData;
struct WorkInfo;

// Perf with FakeAtomic is mildly faster, however if we miss an update that blows the benefit out of the water
class FakeAtomic
{
public:
	void clear(boost::memory_order order = boost::memory_order_seq_cst) { m_fFlag = false; }
	bool test_and_set(boost::memory_order order = boost::memory_order_seq_cst)
		{
		bool m_fRet = m_fFlag;
		m_fFlag = true;
		return m_fRet;
		}
	
private:
	bool m_fFlag;
};

class ThreadWorkManager
{
public:
	ThreadWorkManager(unsigned thread);
	
	// Protocol Methods
	void SubmitServerData(const ServerData &serverData);

	// Miner Methods
	WorkInfo GetWork();
	bool FNewWork();
	bool FTryGetNewWork(WorkInfo *pwinfoCur);

	void ThreadEntry();

	unsigned Thread() { return m_thread; }

private:
	volatile uint8_t rgbGarbage1[64];	// Ensure we take more than a cache line to eliminate false sharing
	std::unique_ptr<WorkInfo> m_spworkInfo;
#ifdef WIN32
	FakeAtomic m_fWaitForWork;
#else
	boost::atomic_flag m_fWaitForWork;
#endif
	const unsigned m_thread;
	volatile uint8_t rgbGarbage2[64];	// Ensure we take more than a cache line to eliminate false sharing
};

class WorkManager
{
public:
	static void SpinupThreads(unsigned cthreads);
	static void StopThreads();
	static void DispatchNewWork(const ServerData &sdata, unsigned thread);
	static void SetSocket(boost::shared_ptr<class TcpConnection> &spconn) { m_wspconn = spconn; }
	static void SubmitFoundChain(Network::Protocol::Submission &submission);

private:
	static std::vector<std::unique_ptr<ThreadWorkManager>> m_vecptwm;
	static boost::weak_ptr<class TcpConnection> m_wspconn;
};
