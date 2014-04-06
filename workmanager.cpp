#include "workmanager.h"
#include "protocol.h"
#include "sync.h"
#include <immintrin.h>

#undef printf

std::vector<std::unique_ptr<ThreadWorkManager>> WorkManager::m_vecptwm;
boost::weak_ptr<TcpConnection> WorkManager::m_wspconn;
static std::unique_ptr<boost::thread_group> m_spminerThreads;

void LocalBitcoinMiner(ThreadWorkManager &workmanager);

ThreadWorkManager::ThreadWorkManager(unsigned thread) : m_thread(thread)
{
	m_fWaitForWork.test_and_set(boost::memory_order_acquire);
}

void ThreadWorkManager::SubmitServerData(const ServerData &serverData)
{
	if (m_spworkInfo != nullptr && serverData.nTime == m_spworkInfo->block.nTime)
		return;
	m_spworkInfo = std::unique_ptr<WorkInfo>(new WorkInfo(serverData));
	_mm_mfence();
	m_fWaitForWork.clear(boost::memory_order_release);
}

WorkInfo ThreadWorkManager::GetWork()
{
	while(m_fWaitForWork.test_and_set(boost::memory_order_acquire))
		{
		boost::this_thread::sleep_for(boost::chrono::milliseconds(1));
		}
	assert(m_spworkInfo != nullptr);
	return *m_spworkInfo;
}

bool ThreadWorkManager::FTryGetNewWork(WorkInfo *pworkinfo)
{
	if (!m_fWaitForWork.test_and_set(boost::memory_order_acquire))
		{
		new (pworkinfo) WorkInfo(*m_spworkInfo);
		return true;
		}
	return false;
}

bool ThreadWorkManager::FNewWork()
{
	if (!m_fWaitForWork.test_and_set(boost::memory_order_acquire))
		{
		m_fWaitForWork.clear();
		return true;
		}
	return false;
}

void ThreadWorkManager::ThreadEntry()
	{
	SetThreadPriority(THREAD_PRIORITY_LOWEST);

	// The wait loop below is to ensure we don't declare the thread as alive until it has its first work item
	while(m_fWaitForWork.test_and_set(boost::memory_order_acquire))
		{
		boost::this_thread::sleep_for(boost::chrono::milliseconds(10));
		}
	m_fWaitForWork.clear();	// GetWork will block if we don't reset this
	
	printf("Thread Alive!\n");
	LocalBitcoinMiner(*this);
	assert(false);
	}

void WorkManager::SpinupThreads(unsigned cthreads)
{
    if (m_spminerThreads != nullptr)
    {
        StopThreads();
    }

	printf("Starting (%u) threads.\n", cthreads);

    m_spminerThreads = std::unique_ptr<boost::thread_group>(new boost::thread_group());
    for (unsigned i = 0; i < cthreads; i++)
    	{
    	ThreadWorkManager *ptwm = new ThreadWorkManager(i);
		m_vecptwm.push_back(std::unique_ptr<ThreadWorkManager>(ptwm));
        m_spminerThreads->create_thread(boost::bind(&ThreadWorkManager::ThreadEntry, ptwm));
    	}
}

void WorkManager::StopThreads()
{
	if (m_spminerThreads != nullptr)
		{
		m_spminerThreads->interrupt_all();
		m_spminerThreads->join_all();
		m_spminerThreads = nullptr;
		}
	m_vecptwm.clear();
}

void WorkManager::DispatchNewWork(const ServerData &sdata, unsigned thread)
{
	if (m_vecptwm.size() > thread)
		{
		m_vecptwm[thread]->SubmitServerData(sdata);
		}
	else
		{
		fprintf(stderr, "Protocol error: Received work for invalid thread.\n");
		}
}

void WorkManager::SubmitFoundChain(Network::Protocol::Submission &submission)
{
	static CCriticalSection csTxChain;
	LOCK(csTxChain);
    std::unique_ptr<uint8_t[]> sprgb(new uint8_t[Network::Protocol::PACKET_BUFFER_MAX]);
	int cbTx = submission.CbSerialize(sprgb.get(), Network::Protocol::PACKET_BUFFER_MAX);

	boost::shared_ptr<TcpConnection> spconn;
	if ((spconn = m_wspconn.lock()) != nullptr)
		spconn->SendToServer(sprgb, cbTx);
}
