#pragma once

#include "packet.h"
#include <iostream>
#include <boost/array.hpp>
#include <boost/asio.hpp>


class TcpConnection
	: public boost::enable_shared_from_this<TcpConnection>
{
public:
	TcpConnection(boost::asio::io_service &io_service);
	void Start(boost::asio::io_service &io_service, const Network::Protocol::Hello &hellopacket, std::string strlabel);

    void SendToServer(std::unique_ptr<uint8_t[]> &sprgb, int cb);

protected:
	void HandleHelloComplete(const boost::system::error_code& error, size_t cbRx);
	void HandleTxWorkSubmissionComplete(const boost::system::error_code& error, size_t cbRx);
	void ProcessPacketHeader(const boost::system::error_code& error, size_t cbRx);
	void ProcessPacketBody(const boost::system::error_code& error, size_t cbRx);

	void ThrowError(const boost::system::error_code &error);
	void ThrowError(const char *szThrow);

	void _DoSendToServer(int cb);

private:
	boost::asio::ip::tcp::socket m_socket;
	uint8_t m_rgbTx[Network::Protocol::PACKET_BUFFER_MAX*2];
	uint8_t m_rgbRx[Network::Protocol::PACKET_BUFFER_MAX];

    std::unique_ptr<uint8_t[]> m_sprgbTx;
	boost::asio::io_service &m_ioservice;
};

