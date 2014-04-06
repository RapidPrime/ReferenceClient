#include "prime.h"
#include "protocol.h"
#include "sha.h"
#include "workmanager.h"
#include "sync.h"

double dPrimesPerSec = 0.0;
double dChainsPerDay = 0.0;
double dBlocksPerDay = 0.0;
int64 nHPSTimerStart = 0;

void SubmitAndCheckWork(ThreadWorkManager &twm, WorkInfo &winfo)
{
	Network::Protocol::Submission submission(winfo.block.nNonce, winfo.bnPrimeChainMultiplier, twm.Thread());
	fprintf(stdout, "Found Work: %x.%s\n", winfo.block.nNonce, winfo.bnPrimeChainMultiplier.ToString(16).c_str());
	WorkManager::SubmitFoundChain(submission);
}

uint256 GenerateHeaderHash(const BlockHeader &blockheader)
	{
	uint256 hashOutput;
	SHA256_CTX ctx;
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, (const uint8_t*)&blockheader, 80);
	SHA256_Final(hashOutput.begin(), &ctx);
	SHA256_Init(&ctx); // is this line needed?
	SHA256_Update(&ctx, hashOutput.begin(), 32);
	SHA256_Final(hashOutput.begin(), &ctx);
	return hashOutput;
	}

bool FUpdateNonce(WorkInfo &winfo, unsigned nMiningProtocol, unsigned nHashFactor, CPrimalityTestParams &testParams)
{
	loop 
		{
		winfo.block.nNonce++;
        if (winfo.block.nNonce >= 0xffff0000)
            break;

        // Check that the hash meets the minimum
        uint256 phash = GenerateHeaderHash(winfo.block);
        if (phash < hashBlockHeaderLimit)
            continue;

        mpz_set_uint256(winfo.mpzHash.get_mpz_t(), phash);
        if (nMiningProtocol >= 2) {
            // Primecoin: Mining protocol v0.2
            // Try to find hash that is probable prime
            if (!ProbablePrimalityTestWithTrialDivision(winfo.mpzHash, 1000, testParams))
                continue;
        } else {
            // Primecoin: Check that the hash is divisible by the fixed primorial
            if (!mpz_divisible_ui_p(winfo.mpzHash.get_mpz_t(), nHashFactor))
                continue;
        }

        // Use the hash that passed the tests
        break;
    	}		
	return (winfo.block.nNonce < 0xffff0000);
}

void LocalBitcoinMiner(ThreadWorkManager &workmanager)
{
	// Primecoin: Allocate data structures for mining
    static CCriticalSection cs;
    CSieveOfEratosthenes sieve;
    CPrimalityTestParams testParams;
    int nAdjustPrimorial = 1; // increase or decrease primorial factor

	unsigned int nPrimorialMultiplier = nPrimorialHashFactor;
    unsigned int nPrimorialMultiplierPrev = nPrimorialMultiplier; // previous primorial factor

	// Used for automatic primorial adjustment
    const unsigned int nRoundSamples = 40; // how many rounds to sample before adjusting primorial
    double dSumBlockExpected = 0.0; // sum of expected blocks
    int64 nSumRoundTime = 0; // sum of round times
    unsigned int nRoundNum = 0; // number of rounds
    double dAverageBlockExpectedPrev = 0.0; // previous average expected blocks per second

    // Primecoin: Check if a fixed primorial was requested
    unsigned int nFixedPrimorial = (unsigned int)GetArg("-primorial", 0);
    if (nFixedPrimorial > 0)
    {
        nFixedPrimorial = std::max(nFixedPrimorial, nPrimorialHashFactor);
        nPrimorialMultiplier = nFixedPrimorial;
    }

	loop
		{
	    //
	    // Search
	    //
	    //int64 nStart = GetTime();
	   	WorkInfo winfo = workmanager.GetWork();
		winfo.block.nNonce = 0;
	    winfo.fNewBlock = true;

	    // Primecoin: Allow choosing the mining protocol version
	    unsigned int nMiningProtocol = (unsigned int)GetArg("-miningprotocol", 1);

	    // Primecoin: try to find hash divisible by primorial
	    unsigned int nHashFactor = PrimorialFast(nPrimorialHashFactor);

	    if (!FUpdateNonce(winfo, nMiningProtocol, nHashFactor, testParams))
			continue;
		// Primecoin: primorial fixed multiplier
		mpz_class mpzPrimorial;
		unsigned int nRoundTests = 0;
		unsigned int nRoundPrimesHit = 0;
		int64 nPrimeTimerStart = GetTimeMicros();
		Primorial(nPrimorialMultiplier, mpzPrimorial);

		loop
		{
			winfo.nTests = 0;
			winfo.nPrimesHit = 0;
			unsigned int vChainsFound[nMaxChainLength];
			for (unsigned int i = 0; i < nMaxChainLength; i++)
				vChainsFound[i] = 0;

			// Meter primes/sec
			static volatile int64 nPrimeCounter = 0;
			static volatile int64 nTestCounter = 0;
			static volatile double dChainExpected = 0.0;
			static volatile double dBlockExpected = 0.0;
			static volatile unsigned int vFoundChainCounter[nMaxChainLength];
			int64 nMillisNow = GetTimeMillis();
			if (nHPSTimerStart == 0)
			{
				nHPSTimerStart = nMillisNow;
				nPrimeCounter = 0;
				nTestCounter = 0;
				dChainExpected = 0.0;
				dBlockExpected = 0.0;
				for (unsigned int i = 0; i < nMaxChainLength; i++)
					vFoundChainCounter[i] = 0;
			}

			// Primecoin: Mining protocol v0.2
			if (nMiningProtocol >= 2)
				winfo.mpzFixedMultiplier = mpzPrimorial;
			else
			{
				if (mpzPrimorial > nHashFactor)
					winfo.mpzFixedMultiplier = mpzPrimorial / nHashFactor;
				else
					winfo.mpzFixedMultiplier = 1;
			}

			// Primecoin: mine for prime chain
			if (MineProbablePrimeChain(workmanager, winfo, vChainsFound, sieve, testParams))
			{
				SetThreadPriority(THREAD_PRIORITY_NORMAL);
				nTotalBlocksFound++;
				SubmitAndCheckWork(workmanager, winfo);
				SetThreadPriority(THREAD_PRIORITY_LOWEST);
			}
			nRoundTests += winfo.nTests;
			nRoundPrimesHit += winfo.nPrimesHit;

#ifdef USE_GCC_BUILTINS
			// Use atomic increment
			__sync_add_and_fetch(&nPrimeCounter, winfo.nPrimesHit);
			__sync_add_and_fetch(&nTestCounter, winfo.nTests);
			__sync_add_and_fetch(&nTotalTests, winfo.nTests);
			for (unsigned int i = 0; i < nMaxChainLength; i++)
			{
				__sync_add_and_fetch(&vTotalChainsFound[i], vChainsFound[i]);
				__sync_add_and_fetch(&vFoundChainCounter[i], vChainsFound[i]);
			}
#else
			nPrimeCounter += winfo.nPrimesHit;
			nTestCounter += winfo.nTests;
			nTotalTests += winfo.nTests;
			for (unsigned int i = 0; i < nMaxChainLength; i++)
			{
				vTotalChainsFound[i] += vChainsFound[i];
				vFoundChainCounter[i] += vChainsFound[i];
			}
#endif

			nMillisNow = GetTimeMillis();
			if (nMillisNow - nHPSTimerStart > 60000)
			{
				LOCK(cs);
				nMillisNow = GetTimeMillis();
				if (nMillisNow - nHPSTimerStart > 60000)
				{
					int64 nTimeDiffMillis = nMillisNow - nHPSTimerStart;
					nHPSTimerStart = nMillisNow;
					double dPrimesPerMinute = 60000.0 * nPrimeCounter / nTimeDiffMillis;
					dPrimesPerSec = dPrimesPerMinute / 60.0;
					//double dTestsPerMinute = 60000.0 * nTestCounter / nTimeDiffMillis;
					dChainsPerDay = 86400000.0 * dChainExpected / nTimeDiffMillis;
					dBlocksPerDay = 86400000.0 * dBlockExpected / nTimeDiffMillis;
					nPrimeCounter = 0;
					nTestCounter = 0;
					dChainExpected = 0;
					dBlockExpected = 0;
					static int64 nLogTime = 0;
					if (nMillisNow - nLogTime > 59000)
					{
						nLogTime = nMillisNow;
						
						PrintCompactStatistics(vFoundChainCounter);
						fprintf(stdout, "\t%3.8f chain/d\n", dChainsPerDay);
					}
				}
			}

			// Check for stop or if block needs to be rebuilt
			boost::this_thread::interruption_point();
			if (winfo.block.nNonce >= 0xffff0000)
				break;
			if (workmanager.FNewWork())
				break;
			if (winfo.fNewBlock)
			{
				// Primecoin: a sieve+primality round completes
				// Primecoin: estimate time to block
				unsigned int nCalcRoundTests = std::max(1u, nRoundTests);
				// Make sure the estimated time is very high if only 0 primes were found
				if (nRoundPrimesHit == 0)
					nCalcRoundTests *= 1000;
				int64 nRoundTime = (GetTimeMicros() - nPrimeTimerStart); 
				double dTimeExpected = (double) nRoundTime / nCalcRoundTests;
				double dRoundChainExpected = (double) nRoundTests;
				unsigned int nTargetLength = TargetGetLength(winfo.block.nBits);
				unsigned int nRequestedLength = nTargetLength;
				// Override target length if requested
				if (nSieveTargetLength > 0)
					nRequestedLength = nSieveTargetLength;
				// Calculate expected number of chains for requested length
				for (unsigned int n = 0; n < nRequestedLength; n++)
				{
					double dPrimeProbability = EstimateCandidatePrimeProbability(nPrimorialMultiplier, n, nMiningProtocol);
					dTimeExpected /= dPrimeProbability;
					dRoundChainExpected *= dPrimeProbability;
				}
				dChainExpected += dRoundChainExpected;
				// Calculate expected number of blocks
				double dRoundBlockExpected = dRoundChainExpected;
				for (unsigned int n = nRequestedLength; n < nTargetLength; n++)
				{
					double dPrimeProbability = EstimateNormalPrimeProbability(nPrimorialMultiplier, n, nMiningProtocol);
					dTimeExpected /= dPrimeProbability;
					dRoundBlockExpected *= dPrimeProbability;
				}
				// Calculate the effect of fractional difficulty
				double dFractionalDiff = GetPrimeDifficulty(winfo.block.nBits) - nTargetLength;
				double dExtraPrimeProbability = EstimateNormalPrimeProbability(nPrimorialMultiplier, nTargetLength, nMiningProtocol);
				double dDifficultyFactor = ((1.0 - dFractionalDiff) * (1.0 - dExtraPrimeProbability) + dExtraPrimeProbability);
				dRoundBlockExpected *= dDifficultyFactor;
				dTimeExpected /= dDifficultyFactor;
				dBlockExpected += dRoundBlockExpected;
				// Calculate the sum of expected blocks and time
                dSumBlockExpected += dRoundBlockExpected;
                nSumRoundTime += nRoundTime;
                nRoundNum++;
                if (nRoundNum >= nRoundSamples)
                {
                    // Calculate average expected blocks per time
                    double dAverageBlockExpected = dSumBlockExpected / ((double) nSumRoundTime / 1000000.0);
                    // Compare to previous value
                    if (dAverageBlockExpected > dAverageBlockExpectedPrev)
                        nAdjustPrimorial = (nPrimorialMultiplier >= nPrimorialMultiplierPrev) ? 1 : -1;
                    else
                        nAdjustPrimorial = (nPrimorialMultiplier >= nPrimorialMultiplierPrev) ? -1 : 1;
                    if (fDebug && GetBoolArg("-printprimorial"))
                        printf("PrimecoinMiner() : Rounds total: num=%u primorial=%u block/s=%3.12f\n", nRoundNum, nPrimorialMultiplier, dAverageBlockExpected);
                    // Store the new value and reset
                    dAverageBlockExpectedPrev = dAverageBlockExpected;
                    nPrimorialMultiplierPrev = nPrimorialMultiplier;
                    dSumBlockExpected = 0.0;
                    nSumRoundTime = 0;
                    nRoundNum = 0;
                }

				// Primecoin: primorial always needs to be incremented if only 0 primes were found
				if (nRoundPrimesHit == 0)
					nAdjustPrimorial = 1;

				// Primecoin: reset sieve+primality round timer
				nPrimeTimerStart = GetTimeMicros();
				nRoundTests = 0;
				nRoundPrimesHit = 0;

				// Primecoin: update time and nonce
				winfo.nTime = std::max(winfo.nTime, (unsigned int) GetAdjustedTime());
				if (!FUpdateNonce(winfo, nMiningProtocol, nHashFactor, testParams))
					break;
				
				// Primecoin: dynamic adjustment of primorial multiplier
				if (nFixedPrimorial == 0 && nAdjustPrimorial != 0) {
					if (nAdjustPrimorial > 0)
					{
						if (!PrimeTableGetNextPrime(nPrimorialMultiplier))
							error("PrimecoinMiner() : primorial increment overflow");
					}
					else if (nPrimorialMultiplier > nPrimorialHashFactor)
					{
						if (!PrimeTableGetPreviousPrime(nPrimorialMultiplier))
							error("PrimecoinMiner() : primorial decrement overflow");
					}
					Primorial(nPrimorialMultiplier, mpzPrimorial);
					nAdjustPrimorial = 0;
				}
			}
		}
	}
}

