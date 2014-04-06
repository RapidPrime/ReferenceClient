#pragma once
#include "bignum.h"
#include <random>

#ifdef LEGACY_BUILD
#include <boost/nondet_random.hpp>
#include <boost/random/uniform_int_distribution.hpp>
#endif

class CBlockHeader;

#ifdef WIN32
#define STARTPACK __pragma(pack(push, 1))
#else
#define STARTPACK
#endif

#ifdef WIN32
#define ENDPACK ; \
	__pragma(pack(pop))
#else
#define ENDPACK __attribute__((packed));
#endif

namespace Network
{
namespace Protocol
{

const size_t PACKET_BUFFER_MAX = 1024;
const int TIMEOUT_INCREMENT_SECONDS = 5;
const int TIMEOUT_MAX_WAIT = 60;

enum class PacketID : int
{
	Ack = 'A',
	Hello = 'H',
	HelloAck = 'h',
	ClientLabel = 'L',
	Message = 'M',
	NOP = 'N',
	Submission = 'S',
	TxWork = 'W'
};

class InvalidPacketException
{
public:
	InvalidPacketException() { };
};

class Packet
{
public:
	PacketID ID() const { return m_id; }

	int CbSerialize(uint8_t *rgbDst, size_t cbMax) const
		{
		assert(cbMax >= PACKET_BUFFER_MAX);
		size_t cb = sizeof(PacketID);
		memcpy(rgbDst, &m_id, cb);
		assert(cb == CbRx());
		cb += CbSerializeInner(rgbDst+cb, cbMax-cb);
		return cb;
		}

	static PacketID IDSnoop(uint8_t *rgbPacket, size_t cb)
		{
		assert(cb >= sizeof(PacketID));
		return *(reinterpret_cast<PacketID*>(rgbPacket));
		}

	virtual ~Packet() { }

	static size_t CbRx() { return sizeof(PacketID); }
	static inline unsigned UnpackUnsigned(const uint8_t *rgb) 
		{ 
		return ((unsigned)rgb[0] | ((unsigned)rgb[1] << 8) | ((unsigned)rgb[2] << 16) | ((unsigned)rgb[3] << 24)); 
		}
	static inline int UnpackInt(const uint8_t *rgb) { return static_cast<int>(UnpackUnsigned(rgb)); }

protected:
	Packet(PacketID id) : m_id(id) { }
	Packet(uint8_t const **prgbRx, size_t &cbRx)
		{
		PacketAssert(cbRx >= sizeof(Packet));
		memcpy(&m_id, *prgbRx, sizeof(PacketID));
		(*prgbRx) += sizeof(PacketID);
		cbRx -= sizeof(PacketID);
		}

	virtual int CbSerializeInner(uint8_t *rgbDst, size_t cbMax) const = 0;

	static void PacketAssert(bool fResult)
		{
		if (!fResult)
			{
			fprintf(stderr, "Packet corrupt\n");
			throw InvalidPacketException();
			}
		}

private:
	PacketID m_id;
};


class Nop : public Packet
{
public:
	Nop() : Packet(PacketID::NOP) { }

protected:
	virtual int CbSerializeInner(uint8_t *rgbDst, size_t cbMax) const override { return 0; }
};


class Submission : public Packet
{
public:
	Submission(unsigned nNonce, CBigNum mpzMultiplier, unsigned thread) : Packet(PacketID::Submission)
		{
		m_nNonce = nNonce;
		m_cbMultiplier = sizeof(uint256);
		uint256 iT = mpzMultiplier.getuint256();
		assert(m_cbMultiplier <= MULTIPLIER_MAX_BYTES);
		memcpy(m_rgbMultiplier, &iT, m_cbMultiplier);

		m_thread = thread;
		m_fRxPacket = false;
		}
	Submission(const uint8_t *rgbRx, size_t cbRx) : Packet(&rgbRx, cbRx)
		{
		PacketAssert(cbRx == sizeof(unsigned)*3 + MULTIPLIER_MAX_BYTES);
		m_nNonce = UnpackUnsigned(rgbRx);
		rgbRx += 4; cbRx -= 4;
		m_cbMultiplier = UnpackUnsigned(rgbRx);
		rgbRx += 4; cbRx -= 4;
		m_thread = UnpackUnsigned(rgbRx);
		rgbRx += 4; cbRx -= 4;

		uint256 iT;
		memcpy(&iT, rgbRx, sizeof(uint256));
		m_bnMultiplier.setuint256(iT);
		
		m_fRxPacket = true;
		}

	unsigned Thread() { return m_thread; }
	void FillBlock(CBlockHeader *pblock);

	inline static size_t CbRxBody() { 
		return sizeof(unsigned)	// m_nNonce
			+ sizeof(unsigned)	// m_cbMultiplier
			+ sizeof(unsigned) // m_thread
			+ MULTIPLIER_MAX_BYTES;
		}
	
	static const unsigned MULTIPLIER_MAX_BYTES = 64; // This should be more than enough. 100-digits should fit in 42 bytes

protected:
	virtual int CbSerializeInner(uint8_t *rgbDst, size_t cbMax) const override
		{
		assert(cbMax >= 8 + m_cbMultiplier);
		memcpy(rgbDst, &m_nNonce, 4);
		memcpy(rgbDst+4, &m_cbMultiplier, 4);
		memcpy(rgbDst+8, &m_thread, 4);
		memcpy(rgbDst+12, m_rgbMultiplier, m_cbMultiplier);
		assert(12U + MULTIPLIER_MAX_BYTES == CbRxBody());
		return (12U + MULTIPLIER_MAX_BYTES);
		}

private:
	unsigned m_nNonce;
	unsigned m_thread;
	uint8_t m_rgbMultiplier[MULTIPLIER_MAX_BYTES];
	unsigned m_cbMultiplier;
	CBigNum m_bnMultiplier;
	bool m_fRxPacket;
};
static_assert(sizeof(Submission) <= PACKET_BUFFER_MAX, "Submission packet must be less than PACKET_BUFFER_MAX");

class TxWork : public Packet
{
public:
STARTPACK
	struct WorkData
	{
		unsigned thread;
        int nVersion;
        uint8_t hashPrevBlock[32];
        uint8_t hashMerkleRoot[32];
        unsigned int nTime;
        unsigned int nBits;
	}
ENDPACK
	
	TxWork(const WorkData &workdata) : Packet(PacketID::TxWork), m_work(workdata) { }

	TxWork(const uint8_t *rgbRx, size_t cbRx) : Packet(&rgbRx, cbRx)
		{
		assert(cbRx == sizeof(WorkData));
		memcpy(&m_work, rgbRx, sizeof(WorkData));
		cbRx -= sizeof(WorkData);
		assert(cbRx == 0);
		}

	const WorkData *PWorkData() { return &m_work; }

	static size_t CbRx() { return sizeof(WorkData) + Packet::CbRx(); }
	static size_t CbRxBody() { return sizeof(WorkData); }

protected:
	virtual int CbSerializeInner(uint8_t *rgbDst, size_t cbMax) const override
		{
		assert(cbMax >= sizeof(WorkData));
		memcpy(rgbDst, &m_work, sizeof(WorkData));
		return sizeof(WorkData);
		}

private:
	WorkData m_work;
};

#undef CB_MSGMAX	// Windows may define this
class Message : public Packet
{
public:
	Message(const char *szmsg) : Packet(PacketID::Message)
		{
		strcpy(m_szmsg, szmsg);
		}
	Message(const uint8_t *rgbRx, size_t cbRx) : Packet(&rgbRx, cbRx)
		{
		assert(cbRx == CB_MSGMAX);
		memcpy(m_szmsg, rgbRx, CB_MSGMAX);
		m_szmsg[CB_MSGMAX-1] = '\0';
		}
	
	static size_t CbRx() { return CB_MSGMAX + Packet::CbRx(); }
	static size_t CbRxBody() { return CB_MSGMAX; }

	void PrintMsg()
		{
		fprintf(stdout, "Server Message: %s\n", m_szmsg);
		}

protected:
	virtual int CbSerializeInner(uint8_t *rgbDst, size_t cbMax) const override
		{
		assert(cbMax >= CB_MSGMAX);
		memcpy(rgbDst, m_szmsg, CB_MSGMAX);
		return CB_MSGMAX;
		}

private:
	static const size_t CB_MSGMAX = 256;
	char m_szmsg[CB_MSGMAX];
};

class ClientLabel : public Packet
{
public:
	ClientLabel(const char *szlabel) : Packet(PacketID::ClientLabel)
		{
		strncpy(m_szmsg, szlabel, CB_LBLMAX);
		m_szmsg[CB_LBLMAX-1] = '\0';
		}
	ClientLabel(const uint8_t *rgbRx, size_t cbRx) : Packet(&rgbRx, cbRx)
		{
		assert(cbRx == CB_LBLMAX);
		memcpy(m_szmsg, rgbRx, CB_LBLMAX);
		m_szmsg[CB_LBLMAX-1] = '\0';
		}

	static size_t CbRxBody() { return CB_LBLMAX; }
	const std::string StrMsg() { return std::string(m_szmsg); }

protected:
	virtual int CbSerializeInner(uint8_t *rgbDst, size_t cbMax) const override
		{
		assert(cbMax >= CB_LBLMAX);
		memcpy(rgbDst, m_szmsg, CB_LBLMAX);
		return CB_LBLMAX;
		}
	
private:
	static const size_t CB_LBLMAX = 64;
	char m_szmsg[CB_LBLMAX];
};

class Hello : public Packet
{
public:
STARTPACK
    struct HelloData
	{
    HelloData()
        {
        }

	void Randomize()
		{
#ifdef LEGACY_BUILD
        boost::random_device generator;
        boost::random::uniform_int_distribution<int> distribution(INT_MIN, INT_MAX);
#else
        std::random_device generator;
        std::uniform_int_distribution<int> distribution(INT_MIN, INT_MAX);
#endif
		  for (int i=0; i<256; i++) {
		    rgbEntropy[i] = static_cast<unsigned char>(distribution(generator));
		  }
		}
	// Program Ident
	uint32_t version;
	uint32_t build;
	uint32_t protocolVersion;

	unsigned char cthreads;
	char pad[3];
	
	// CPUID
	uint32_t vendor;
	union 
		{
		uint32_t eaxProcInfo;
		struct
			{
			uint32_t stepping : 4;
			uint32_t model : 4;
			uint32_t family : 4;
			uint32_t type : 2;
			uint32_t exmodel : 4;
			uint32_t exfamily : 8;
			};
		};

	// Payout Address
    // max len for an address is 34 according to some wikis
    // careful, not null terminated, use SafeAddress()
    uint8_t rgbAddr[34];

	// Random Data
	uint8_t rgbEntropy[256];
	}
ENDPACK

	Hello(const HelloData &hellodata) : Packet(PacketID::Hello), m_data(hellodata) { }
	Hello(const uint8_t *rgbRx, size_t cbRx) : Packet(&rgbRx, cbRx)
		{
		assert(cbRx == sizeof(HelloData));
		memcpy(&m_data, rgbRx, sizeof(HelloData));
		cbRx -= sizeof(HelloData);
		assert(cbRx == 0);
		}
		
	const HelloData *PHelloData() const { return &m_data; }
	static size_t CbRxBody() { return sizeof(HelloData); }
	static size_t CbRx() { return CbRxBody() + Packet::CbRx(); }

    std::string SafeAddress() { return std::string((const char*)m_data.rgbAddr, sizeof(m_data.rgbAddr)); }

protected:
	virtual int CbSerializeInner(uint8_t *rgbDst, size_t cbMax) const override
		{
		assert(cbMax >= sizeof(HelloData));
		memcpy(rgbDst, &m_data, sizeof(HelloData));
		return sizeof(HelloData);
		}

private:
	HelloData m_data;
};

class HelloAck : public Packet
	{
public:
	static size_t CbRxBody() 
		{
		return 256;	// Reserve space for server data
		}
	};

};	// Protocol
};	// Network
