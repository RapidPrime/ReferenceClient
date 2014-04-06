#include <stdlib.h>
#include <stdio.h>
#include <memory>
#include "versions.h"
#include "workmanager.h"
#include "protocol.h"
#include "cpuid.h"
#include "tcpconnection.h"
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include "base58.h"

bool fTestNet = false;
Network::Protocol::Hello::HelloData hellodata;

#undef printf

void InitPrimeMiner();

void PrintVersionInfo()
{
	printf("********************************************************************************\n");
	printf("\tRapidPrime miner Version: %04d.%04d, Protocol Version: %d\n", CLIENT_VERSION, CLIENT_BUILD, PROTOCOL_VERSION);
	printf("\tCopyright (C) 2014 - The RapidPrime developers\n");
	printf("\tCopyright (C) 2014 - The Primecoin and Bitcoin developers.\n");
	printf("********************************************************************************\n\n");
}

void PrintHelp()
{
	printf("Usage: rapidprime --payment-address=[PAYMENT_ADDRESS]\n");
	printf("Options: \n");
	printf("\t-threads=[n]                   Specifies number of threads to use.\n");
	printf("\t-label=[name of client]        Specify a label to track the worker on the miner stats page.\n");
	printf("\t-label-is-hostname             Use the hostname of this machine as the label.\n");
	printf("\t-license                       View license information\n");

	printf("\n");
}

void PrintLicense();

int main(int argc, char *argv[])
{
	ParseParameters(argc, argv);	
    fTestNet = GetBoolArg("-testnet");
	PrintVersionInfo();
	if (GetBoolArg("-license"))
		PrintLicense();
	CBitcoinAddress payoutaddress;

	// Validate Required Parameters
	if (mapArgs.count("--payment-address"))
		{
        payoutaddress.SetString(mapArgs["--payment-address"]);
		if (!payoutaddress.IsValid())
			{
			fprintf(stderr, "Error: Invalid payout address\n");
			return 0;
			}
		}
	else
		{
		PrintHelp();
		return 0;
		}

	assert(payoutaddress.IsValid());

	std::string strlbl = GetArg("-label", "");
	if (GetBoolArg("-label-is-hostname", false))
		{
		strlbl = boost::asio::ip::host_name();
		}
	if (strlbl.length() > 0)
		{
		printf("Sending host label: %s\n", strlbl.c_str());
		}
	
	InitPrimeMiner();
	int cssleepCounter = 0;
	int cthreads = GetArg("-threads", boost::thread::hardware_concurrency());
	if (cthreads == 0)
		{
		cthreads = boost::thread::hardware_concurrency();
		}

	if (cthreads > 64)
		{
		fprintf(stderr, "Warning: Truncating threads to 64.\n");
		cthreads = 64;
		}

	hellodata.version = CLIENT_VERSION;
	hellodata.build = CLIENT_BUILD;
	hellodata.protocolVersion = PROTOCOL_VERSION;
	hellodata.cthreads = static_cast<unsigned char>(cthreads);

    auto& addr = mapArgs["--payment-address"];
    if (addr.size() > sizeof(hellodata.rgbAddr)) {
        fprintf(stderr, "Error: Address length is too long!\n");
        return 0;
    }
    memset(hellodata.rgbAddr, 0, sizeof(hellodata.rgbAddr));
    memcpy(hellodata.rgbAddr, addr.c_str(), addr.size());

	// Find CPU features for better tuning
	CPUID cpuid;
	cpuid.load(0);
	hellodata.vendor = cpuid.EBX() ^ cpuid.EDX() ^ cpuid.ECX();
	cpuid.load(1); 	//proc info & features
	hellodata.eaxProcInfo = cpuid.EAX();

	for (;;)
		{
		hellodata.Randomize();
		Network::Protocol::Hello txhello(hellodata);
		try{	
			WorkManager::SpinupThreads(cthreads);
			boost::asio::io_service io_service;
			boost::shared_ptr<TcpConnection> spconn(new TcpConnection(io_service));
			spconn->Start(io_service, txhello, strlbl);
			WorkManager::SetSocket(spconn);
			while(true)
				io_service.run();
		}
		catch (std::exception& e)
		{
		  std::cerr << "Connection closed: " << e.what() << std::endl;
		}
		WorkManager::StopThreads();
		cssleepCounter += Network::Protocol::TIMEOUT_INCREMENT_SECONDS;
        cssleepCounter = std::min(Network::Protocol::TIMEOUT_MAX_WAIT, cssleepCounter);
        sleep(hellodata.rgbEntropy[0] % cssleepCounter);
		}

	printf("Goodbye!\n");
}
