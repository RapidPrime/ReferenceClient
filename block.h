class CBlockIndex;
class CTransaction;

#include "script.h"

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    // header
    static const int CURRENT_VERSION=2;
    int nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    unsigned int nTime;
    unsigned int nBits;  // Primecoin: prime chain target, see prime.cpp
    unsigned int nNonce;

    // Primecoin: proof-of-work certificate
    // Multiplier to block hash to derive the probable prime chain (k=0, 1, ...)
    // Cunningham Chain of first kind:  hash * multiplier * 2**k - 1
    // Cunningham Chain of second kind: hash * multiplier * 2**k + 1
    // BiTwin Chain:                    hash * multiplier * 2**k +/- 1
    CBigNum bnPrimeChainMultiplier;

    CBlockHeader()
    {
        SetNull();
    }

    void SetNull()
    {
        nVersion = CBlockHeader::CURRENT_VERSION;
        hashPrevBlock = 0;
        hashMerkleRoot = 0;
        nTime = 0;
        nBits = 0;
        nNonce = 0;
        bnPrimeChainMultiplier = 0;
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    // Primecoin: header hash does not include prime certificate
    uint256 GetHeaderHash() const
    {
        return Hash(BEGIN(nVersion), END(nNonce));
    }

    int64 GetBlockTime() const
    {
        return (int64)nTime;
    }

};


