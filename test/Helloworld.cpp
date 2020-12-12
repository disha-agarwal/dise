#include <cryptoTools/Common/CLP.h>
#include <cryptoTools/Common/Timer.h>
#include <cryptoTools/Network/IOService.h>
#include <dEnc/distEnc/AmmrClient.h>
#include <dEnc/dprf/Npr03SymDprf.h>
#include "GroupChannel.h"
#include "RandomNodePicker.cpp"
#include "util.h"


using namespace osuCrypto;

static const std::vector<std::string> ips {"10.0.60.177", "10.0.60.64", "10.0.60.185"};


template<typename DPRF>
void eval(dEnc::AmmrClient<DPRF>& enc, u64 n, u64 m, u64 blockCount, u64 batch, u64 trials, u64 numAsync, bool lat, std::string tag)
{
    Timer t;
    auto s = t.setTimePoint("start");

    // The party that will initiate the encryption.
    // The other parties will respond to requests on the background.
    // This happens using the threads created by IOService
    auto& initiator = enc;

    // the buffers to hold the data.
    std::vector<std::vector<block>> data(batch), ciphertext(batch);
    for (auto& d : data) d.resize(blockCount);

    // we are interested in latency and therefore we 
    // will only have one encryption in flight at a time.
    for (u64 t = 0; t < trials; ++t) 
        initiator.encrypt(data[0], ciphertext[0]);

    auto e = t.setTimePoint("end");

    enc.close();

    auto online = (double)std::chrono::duration_cast<std::chrono::milliseconds>(e - s).count();

    // print the statistics.
    std::cout << tag <<"      n:" << n << "  m:" << m << "   t:" << trials << "     enc/s:" << 1000 * trials / online << "   ms/enc:" << online / trials << " \t "
        << " Mbps:" << (trials * sizeof(block) * 2 * (m - 1) * 8 / (1 << 20)) / (online / 1000)
        << std::endl;
}


void AmmrSymClient_tp_Perf_test(u64 n, u64 m, u64 blockCount, u64 trials, u64 numAsync, u64 batch, bool lat)
{
    // set up the networking
    IOService ios;
    GroupChannel gc;
    gc.connect(ips, n, ios);

    oc::block seed;
    if(gc.current_node == 0)
    {
        // Initialize the parties using a random seed from the OS.
        seed = sysRandomSeed();
        for (int i = 1; i < n; i++)
        {
            gc.nChannels[i-1].send(seed);
        }
    } else 
    {
        gc.nChannels[0].recv(seed);
    }

    // allocate the DPRFs and the encryptors
    dEnc::AmmrClient<dEnc::Npr03SymDprf> enc;
    dEnc::Npr03SymDprf dprf;

    PRNG prng(seed);
    // Generate the master key for this DPRF.
    dEnc::Npr03SymDprf::MasterKey mk;
    mk.KeyGen(n, m, prng);

    // initialize the DPRF and the encrypters
    dprf.init(gc.current_node, m, gc.nChannels, gc.nChannels, prng.get<block>(), mk.keyStructure, mk.getSubkey(gc.current_node));
    enc.init(gc.current_node, prng.get<block>(), &dprf);
    
    // Perform the benchmark.                                          
    eval(enc, n, m, blockCount, batch, trials, numAsync, lat, "Sym      ");
}


int main(int argc, char** argv) {
    CLP cmd;
    cmd.parse(argc, argv);

    u64 n = cmd.get<u64>("n");
    RandomNodePicker nodePicker(n);
    std::cout << "Generators for n=" << n << " are ";
    for(int i: nodePicker.generators)
        std::cout << i << " ";
    std::cout << std::endl;

    std::cout << "Picked Node: " << nodePicker.nextNode() << std::endl;

    // u64 n = ips.size();
    // // getLatency(ips, n);

    // u64 t = 4096;
    // u64 b = 128;
    // u64 a = 1024 / b;
    // cmd.setDefault("t", t);
    // cmd.setDefault("b", b);
    // cmd.setDefault("a", a);
    // cmd.setDefault("size", 20);
    // t = cmd.get<u64>("t");
    // b = cmd.get<u64>("b");
    // a = cmd.get<u64>("a");
    // auto size = cmd.get<u64>("size");
    // bool l = cmd.isSet("l");

    // cmd.setDefault("mf", "0.5");
    // auto mFrac = cmd.get<double>("mf");
    // if (mFrac <= 0 || mFrac > 1)
    // {
    //     std::cout << ("bad mf") << std::endl;
    //     return 0;
    // }

    // cmd.setDefault("mc", -1);
    // auto mc = cmd.get<i64>("mc");

    // auto m = std::max<u64>(2, (mc == -1) ? n * mFrac : mc);
    // m = 3;

    // if (m > n)
    // {
    //     std::cout << "can not have a threshold larger than the number of parties. theshold=" << m << ", #parties=" << n << std::endl;
    //     return -1;
    // }

    // AmmrSymClient_tp_Perf_test(n, m, size, t, a, b, l);
    return 0;
}