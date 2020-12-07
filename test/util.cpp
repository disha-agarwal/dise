#include "util.h"

using namespace osuCrypto;
#include <cryptoTools/Common/Log.h>
#include <cryptoTools/Common/Timer.h>
#include <cryptoTools/Network/IOService.h>
#include <cryptoTools/Network/Endpoint.h>
#include <cryptoTools/Network/Channel.h>
#define tryCount 2


std::string exec(const char* cmd) 
{
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

std::string getIP()
{
    std::string result = exec("ifconfig");
    std::istringstream iss(result);
    std::vector<std::string> tokens{std::istream_iterator<std::string>{iss},
                                    std::istream_iterator<std::string>{}};

    std::string ip;
    for(int i=0; i< tokens.size(); i++)
    {
        if(tokens[i].compare("inet") == 0)
        {
            ip = tokens[i+1];
            break;
        } 
    }

    return ip;
}

void getLatency(std::vector<std::string> ips, u64 n)
{
    IOService ios(n);
    ios.showErrorMessages(true);

    std::string ip = getIP();
    u64 current_node; // index of current node's ip in vector of ips
    for(int i = 0; i < n ; i++) {
        if(ips[i].compare(ip) == 0) {
            current_node = i;    
        }
    }

    for (int i = 0; i < n ; i++) {
        if(i < current_node)
        {
            Channel clientChl = Session(ios, ips[i], SessionMode::Client).addChannel();
            clientChl.send(getIP());
            std::string msg;
            clientChl.recv(msg);
            std::cout << "Received " << msg << std::endl;
            recverGetLatency(clientChl);    
        }
        else if(i == current_node)
        {
            continue;
        }
        else
        {
            Channel serverChl = Session(ios, ip, SessionMode::Server).addChannel();
            std::chrono::milliseconds timeout(1000000000000);
            serverChl.waitForConnection();
            serverChl.send(getIP());
            std::string msg;
            serverChl.recv(msg);
            std::cout << "Received " << msg << std::endl;
            senderGetLatency(serverChl);
        }
    }
}

void senderGetLatency(Channel& chl)
{
    u8 dummy[1];

    chl.asyncSend(dummy, 1);

    chl.recv(dummy, 1);
    chl.asyncSend(dummy, 1);

    std::vector<u8> oneMbit((1 << 20) / 8);
    for (u64 i = 0; i < tryCount; ++i)
    {
        chl.recv(dummy, 1);

        for(u64 j =0; j < (1<<10); ++j)
            chl.asyncSend(oneMbit.data(), oneMbit.size());
    }
    chl.recv(dummy, 1);

	for (u64 j = 0; j < (1 << 10); ++j)
	{
		chl.asyncRecv(oneMbit.data(), oneMbit.size());
		chl.asyncSend(oneMbit.data(), oneMbit.size());
	}

	chl.recv(dummy, 1);
}

void recverGetLatency(Channel& chl)
{
    u8 dummy[1];
    chl.recv(dummy, 1);
    Timer timer;
    auto start = timer.setTimePoint("");
    chl.asyncSend(dummy, 1);

    chl.recv(dummy, 1);

    auto mid = timer.setTimePoint("");
    auto recvStart = mid;
    auto recvEnd = mid;

    auto rrt = mid - start;
    std::cout << "latency:   " << std::chrono::duration_cast<std::chrono::milliseconds>(rrt).count() << " ms" << std::endl;

    std::vector<u8> oneMbit((1 << 20) / 8);
    for (u64 i = 0; i < tryCount; ++i)
    {
        recvStart = timer.setTimePoint("");
        chl.asyncSend(dummy, 1);

        for (u64 j = 0; j < (1 << 10); ++j)
            chl.recv(oneMbit);

        recvEnd = timer.setTimePoint("");

        // nanoseconds per GegaBit
        auto uspGb = std::chrono::duration_cast<std::chrono::nanoseconds>(recvEnd - recvStart - rrt / 2).count();

        // nanoseconds per second
        double usps = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(1)).count();

        // MegaBits per second
        auto Mbps = usps / uspGb *  (1 << 10);

        std::cout << "bandwidth ->: " << Mbps << " Mbps" << std::endl;
    }

    chl.asyncSend(dummy, 1);
	std::future<void> f;
	recvStart = timer.setTimePoint("");
	for (u64 j = 0; j < (1 << 10); ++j)
	{
		f = chl.asyncRecv(oneMbit.data(), oneMbit.size());
		chl.asyncSend(oneMbit.data(), oneMbit.size());
	}
	f.get();
	recvEnd = timer.setTimePoint("");
	chl.send(dummy, 1);

	// nanoseconds per GegaBit
	auto uspGb = std::chrono::duration_cast<std::chrono::nanoseconds>(recvEnd - recvStart - rrt / 2).count();

	// nanoseconds per second
	double usps = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(1)).count();

	// MegaBits per second
	auto Mbps = usps / uspGb *  (1 << 10);

	std::cout << "bandwidth <->: " << Mbps << " Mbps" << std::endl;
}
