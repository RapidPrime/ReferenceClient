// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef H_BITCOIN_SCRIPT
#define H_BITCOIN_SCRIPT

#include <string>
#include <vector>

#include <boost/variant.hpp>

#include "bignum.h"

class CCoins;
class CKeyStore;



class CNoDestination {
public:
    friend bool operator==(const CNoDestination &a, const CNoDestination &b) { return true; }
    friend bool operator<(const CNoDestination &a, const CNoDestination &b) { return true; }
};

/** A txout script template with a specific destination. It is either:
 *  * CNoDestination: no destination set
 *  * CKeyID: TX_PUBKEYHASH destination
 *  * CScriptID: TX_SCRIPTHASH destination
 *  A CTxDestination is the internal data type encoded in a CBitcoinAddress
 */
typedef boost::variant<CNoDestination, CKeyID, CScriptID> CTxDestination;

#endif
