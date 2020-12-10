#include <cryptoTools/Common/CLP.h>
#include <cryptoTools/Common/Timer.h>
#include <cryptoTools/Network/IOService.h>
#include <dEnc/distEnc/AmmrClient.h>
#include <dEnc/dprf/Npr03SymDprf.h>
#include "GroupChannel.h"
#include "util.h"


using namespace osuCrypto;

static const std::vector<std::string> ips {"10.0.60.177", "10.0.60.64", "10.0.60.185"};


template<typename DPRF>
void eval(std::vector<dEnc::AmmrClient<DPRF>>& encs, u64 n, u64 m, u64 blockCount, u64 batch, u64 trials, u64 numAsync, bool lat, std::string tag)
{
    std::cout << "Inside eval()" << std::endl;

    Timer t;
    auto s = t.setTimePoint("start");

    // The party that will initiate the encryption.
    // The other parties will respond to requests on the background.
    // This happens using the threads created by IOService
    auto& initiator = encs[0];

    // the buffers to hold the data.
    std::vector<std::vector<block>> data(batch), ciphertext(batch), data2(batch);
    for (auto& d : data) d.resize(blockCount);

    if (lat)
    {
        // we are interested in latency and therefore we 
        // will only have one encryption in flight at a time.
        for (u64 t = 0; t < trials; ++t)
            initiator.encrypt(data[0], ciphertext[0]);
    }
    else
    {
        // We are going to initiate "batch" encryptions at once.
        // In addition, we will send out "numAsync" batches before we 
        // complete the first batch. This allows higher throughput.

        // A place to store "inflight" encryption operations
        std::deque<dEnc::AsyncEncrypt> asyncs;
        
        auto loops = (trials + batch - 1) / batch;
        trials = loops * batch;
        for (u64 t = 0; t < loops; ++t)
        {
            // check if we have reached the maximum number of 
            // async encryptions. If so, then complete the oldest 
            // one by calling AsyncEncrypt::get();
            if (asyncs.size() == numAsync)
            {
                asyncs.front().get();
                asyncs.pop_front();
            }

            // initiate another encryption. This will not complete immidiately.
            asyncs.emplace_back(initiator.asyncEncrypt(data, ciphertext));
        }

        // Complete all pending encryptions
        while (asyncs.size())
        {
            asyncs.front().get();
            asyncs.pop_front();
        }
    }

    auto e = t.setTimePoint("end");

    // close all of the instances.
    for (u64 i = 0; i < n; ++i)
        encs[i].close();

    auto online = (double)std::chrono::duration_cast<std::chrono::milliseconds>(e - s).count();

    // print the statistics.
    std::cout << tag <<"      n:" << n << "  m:" << m << "   t:" << trials << "     enc/s:" << 1000 * trials / online << "   ms/enc:" << online / trials << " \t "
        << " Mbps:" << (trials * sizeof(block) * 2 * (m - 1) * 8 / (1 << 20)) / (online / 1000)
        << std::endl;
}


void AmmrSymClient_tp_Perf_test(u64 n, u64 m, u64 blockCount, u64 trials, u64 numAsync, u64 batch, bool lat)
{
    std::cout << "Inside AmmrSymClient_tp_Perf_test()" << std::endl;

    // set up the networking
    IOService ios;
    GroupChannel eps;
    std::cout << "Connecting to GroupChannel" << std::endl;
    eps.connect(ips, n, ios);
    std::cout << "Connected successfully!" << std::endl;

    if(eps.current_node == 0)
    {
        // allocate the DPRFs and the encryptors
        std::vector<dEnc::AmmrClient<dEnc::Npr03SymDprf>> encs(n);
        std::vector<dEnc::Npr03SymDprf> dprfs(n);

        // Initialize the parties using a random seed from the OS.
        PRNG prng(sysRandomSeed());

        // Generate the master key for this DPRF.
        dEnc::Npr03SymDprf::MasterKey mk;
        mk.KeyGen(n, m, prng);
        std::cout << "Keygen done!" << std::endl;

        for(int i = 0; i<n; i++)
        {
            std::cout << "i: " << i << std::endl;
            // initialize the DPRF and the encrypters
            dprfs[i].init(eps.current_node, m, eps.nChannels, eps.nChannels, prng.get<block>(), mk.keyStructure, mk.getSubkey(i));
            encs[i].init(eps.current_node, prng.get<block>(), &dprfs[i]);
        }

    }

    // Perform the benchmark.                                          
    // eval(encs, n, m, blockCount, batch, trials, numAsync, lat, "Sym      ");


}


int main(int argc, char** argv) {
    CLP cmd;
    cmd.parse(argc, argv);

    u64 n = ips.size();
    //getLatency(ips, n);
    n = 4;

    u64 t = 4096;
    u64 b = 128;
    u64 a = 1024 / b;
    cmd.setDefault("t", t);
    cmd.setDefault("b", b);
    cmd.setDefault("a", a);
    cmd.setDefault("size", 20);
    t = cmd.get<u64>("t");
    b = cmd.get<u64>("b");
    a = cmd.get<u64>("a");
    auto size = cmd.get<u64>("size");
    bool l = cmd.isSet("l");

    cmd.setDefault("mf", "0.5");
    auto mFrac = cmd.get<double>("mf");
    if (mFrac <= 0 || mFrac > 1)
    {
        std::cout << ("bad mf") << std::endl;
        return 0;
    }

    cmd.setDefault("mc", -1);
    auto mc = cmd.get<i64>("mc");

    auto m = std::max<u64>(2, (mc == -1) ? n * mFrac : mc);
    m = 3;

    if (m > n)
    {
        std::cout << "can not have a threshold larger than the number of parties. theshold=" << m << ", #parties=" << n << std::endl;
        return -1;
    }

    AmmrSymClient_tp_Perf_test(n, m, size, t, a, b, l);

    return 0;
}