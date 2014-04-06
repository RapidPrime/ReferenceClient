#include "tcpconnection.h"
#include "bignum.h"
#include "uint256.h"
#include "protocol.h"
#include "workmanager.h"
#ifdef WIN32
#include <immintrin.h>
#else
#include <x86intrin.h>
#endif

using boost::asio::ip::tcp;
extern Network::Protocol::Hello::HelloData hellodata;

TcpConnection::TcpConnection(boost::asio::io_service &io_service)
	: m_socket(io_service), m_ioservice(io_service)
{
}

void TcpConnection::Start(boost::asio::io_service &io_service, const Network::Protocol::Hello &hellopacket, std::string strlabel)
{	
	tcp::resolver resolver(io_service);

	boost::system::error_code error = boost::asio::error::host_not_found;
	const std::string rgservers[] = { "pool1.rapidprime.com", "pool2.rapidprime.com", "pool3.rapidprime.com", "pool.rapidprime.com" };
	for (const std::string &server : rgservers)
		{
			tcp::resolver::query query(server, "38204");
			try
				{
				tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
				tcp::resolver::iterator end;
				while (error && endpoint_iterator != end)
					{
					m_socket.close();
					m_socket.connect(*endpoint_iterator, error);
					++endpoint_iterator;
					}
				if (!error)
					{
					printf("Connecting to: %s\n", server.c_str());
					break;
					}
				}
			catch(...)
				{
				continue;
				}
		}
	if (error || !m_socket.is_open())
		ThrowError(error);

	// The TCP connection is open.	Lets let the server know who we are
	int cbTx = hellopacket.CbSerialize(m_rgbTx, Network::Protocol::PACKET_BUFFER_MAX);
	if (strlabel.length() > 0)
		{
		Network::Protocol::ClientLabel clientlabel(strlabel.c_str());
		cbTx += clientlabel.CbSerialize(m_rgbTx+cbTx, Network::Protocol::PACKET_BUFFER_MAX);
		}

	boost::asio::async_write(m_socket, boost::asio::buffer(m_rgbTx, cbTx),
		boost::bind(&TcpConnection::HandleHelloComplete, shared_from_this(),
			boost::asio::placeholders::error,
			boost::asio::placeholders::bytes_transferred));
}

void TcpConnection::HandleHelloComplete(const boost::system::error_code& error, size_t cbRx)
{
	if (error)
		ThrowError(error);

	boost::asio::async_read(m_socket, boost::asio::buffer(m_rgbRx, sizeof(Network::Protocol::PacketID)),
		boost::bind(&TcpConnection::ProcessPacketHeader, shared_from_this(),
			boost::asio::placeholders::error,
			boost::asio::placeholders::bytes_transferred));
}

void TcpConnection::ProcessPacketHeader(const boost::system::error_code& error, size_t cbRx)
{
	if (error)
		ThrowError(error);
	if (cbRx != sizeof(Network::Protocol::PacketID))
		ThrowError("Invalid receipt");

	Network::Protocol::PacketID id = (Network::Protocol::PacketID) Network::Protocol::Packet::UnpackUnsigned(m_rgbRx);
	assert(cbRx == sizeof(Network::Protocol::PacketID));
	size_t cbExpect = 0;

	switch(id)
		{
	case Network::Protocol::PacketID::TxWork:
		cbExpect = Network::Protocol::TxWork::CbRx() - sizeof(id);
		break;
		
	case Network::Protocol::PacketID::Message:
		cbExpect = Network::Protocol::Message::CbRxBody();
		break;

	case Network::Protocol::PacketID::HelloAck:
		cbExpect = Network::Protocol::HelloAck::CbRxBody();
		break;
		
	default:
		fprintf(stderr, "Packet type: %c\n", id);
		ThrowError("Unknown packet");
		}

	boost::asio::async_read(m_socket, boost::asio::buffer(m_rgbRx+cbRx, cbExpect),
		boost::bind(&TcpConnection::ProcessPacketBody, shared_from_this(),
			boost::asio::placeholders::error,
			boost::asio::placeholders::bytes_transferred));
}

void TcpConnection::ProcessPacketBody(const boost::system::error_code& error, size_t cbRx)
{
	if (error)
		ThrowError(error);

	Network::Protocol::PacketID id = (Network::Protocol::PacketID) Network::Protocol::Packet::UnpackUnsigned(m_rgbRx);
	switch(id)
		{
	case Network::Protocol::PacketID::TxWork:
		{
		std::unique_ptr<Network::Protocol::Packet> sppacket;
		size_t cbExpect = Network::Protocol::TxWork::CbRxBody();
		if (cbRx != cbExpect)
			ThrowError("Not enough data");
		
		sppacket = std::unique_ptr<Network::Protocol::Packet>(new Network::Protocol::TxWork(m_rgbRx, cbRx+sizeof(Network::Protocol::PacketID)));
		Network::Protocol::TxWork *pwork = static_cast<Network::Protocol::TxWork*>(sppacket.get());
		ServerData sdata;
		sdata.nVersion = pwork->PWorkData()->nVersion;
		memcpy(sdata.hashMerkleRoot.begin(), pwork->PWorkData()->hashMerkleRoot, 32);
		memcpy(sdata.hashPrevBlock.begin(), pwork->PWorkData()->hashPrevBlock, 32);
		sdata.nTime = pwork->PWorkData()->nTime;
		sdata.nBits = pwork->PWorkData()->nBits;

		WorkManager::DispatchNewWork(sdata, pwork->PWorkData()->thread);
		}
		break;

	case Network::Protocol::PacketID::Message:
		{
		size_t cbExpect = Network::Protocol::Message::CbRxBody();
		if (cbRx != cbExpect)
			ThrowError("Not enough data");
		Network::Protocol::Message msg(m_rgbRx, cbRx+sizeof(Network::Protocol::PacketID));
		msg.PrintMsg();
		}
		break;

	case Network::Protocol::PacketID::HelloAck:
		break;
		
	default:
		ThrowError("Unknown packet body");
		}


	// Await new packet
	boost::asio::async_read(m_socket, boost::asio::buffer(m_rgbRx, sizeof(Network::Protocol::PacketID)),
		boost::bind(&TcpConnection::ProcessPacketHeader, shared_from_this(),
			boost::asio::placeholders::error,
			boost::asio::placeholders::bytes_transferred));
}

void TcpConnection::HandleTxWorkSubmissionComplete(const boost::system::error_code& error, size_t cbRx)
{
	m_sprgbTx = nullptr;
	if (error)
		ThrowError(error);
}

void TcpConnection::SendToServer(std::unique_ptr<uint8_t[]> &sprgb, int cb)
{
	while(m_sprgbTx != nullptr)
		{
#ifndef __MIC__
		_mm_pause();
#endif
		}
	m_sprgbTx = std::move(sprgb);
	m_ioservice.post(boost::bind(&TcpConnection::_DoSendToServer, shared_from_this(), cb));
}

void TcpConnection::_DoSendToServer(int cb)
{
	boost::asio::async_write(m_socket, boost::asio::buffer(m_sprgbTx.get(), cb),
		boost::bind(&TcpConnection::HandleTxWorkSubmissionComplete, shared_from_this(),
			boost::asio::placeholders::error,
			boost::asio::placeholders::bytes_transferred));
}

void TcpConnection::ThrowError(const boost::system::error_code &error)
{
	assert(error);
	m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
	m_socket.close();
	throw boost::system::system_error(error);
}

void TcpConnection::ThrowError(const char *szThrow)
{
	m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
	m_socket.close();
	throw std::runtime_error(szThrow);
}
