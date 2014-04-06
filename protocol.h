#pragma once

#include "uint256.h"
#include "bignum.h"
#include <gmp.h>
#include <gmpxx.h>


struct ServerData
{
	int nVersion;
	uint256 hashPrevBlock;
	uint256 hashMerkleRoot;
	unsigned int nTime;
	unsigned int nBits;  
};


struct BlockHeader
{
	BlockHeader(const ServerData &serverdata) : 
		nVersion(serverdata.nVersion),
		hashPrevBlock(serverdata.hashPrevBlock),
		hashMerkleRoot(serverdata.hashMerkleRoot),
		nTime(serverdata.nTime),
		nBits(serverdata.nBits),
		nNonce(0)
		{
		}
	
	const int nVersion;
	const uint256 hashPrevBlock;
	const uint256 hashMerkleRoot;
	const unsigned int nTime;
	const unsigned int nBits;  
	unsigned int nNonce; // Client Generated
};
static_assert(sizeof(BlockHeader) == 80, "BlockHeader header is expected to be 80 bytes");

struct WorkInfo
{
    WorkInfo(const ServerData &serverdata) : block(serverdata), fNewBlock(true), nTime(0)
		{
		}

	// BlockHeader Data
	BlockHeader block;

	// Inputs
	mpz_class mpzFixedMultiplier;
	mpz_class mpzHash;

	// InOut
	bool fNewBlock;

	// Outputs
	CBigNum bnPrimeChainMultiplier;	// Found work result

	// Stats (output)
	unsigned int nTests;
	unsigned nPrimesHit;

	// Review if these are required:
	unsigned nTime;
};

