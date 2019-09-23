#include <map>
#include "HCADecodeService.h"
#include "clHCA.h"

HCADecodeService::HCADecodeService()
    : numthreads(std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 1),
      numchannels(0),
      chunksize(24),
      blocklistsz(0),
      blocks(nullptr),
      workingrequest(nullptr),
      workerthreads(new std::thread[this->numthreads]),
      workersem(new Semaphore[this->numthreads]),
      datasem(0),
      mainsem(0),
      channels(new clHCA::stChannel[0x10 * this->numthreads]),
      wavebuffer(new float[0x10 * 0x80 * this->numthreads]),
      shutdown(false),
      stopcurrent(false)
{
    for (unsigned int i = 0; i < this->numthreads; ++i)
    {
        workerthreads[i] = std::thread(&HCADecodeService::Decode_Thread, this, i);
    }
    dispatchthread = std::thread(&HCADecodeService::Main_Thread, this);
}

HCADecodeService::HCADecodeService(unsigned int numthreads, unsigned int chunksize)
    : numthreads(numthreads ? numthreads : (std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 1)),
      numchannels(0),
      chunksize(chunksize ? chunksize : 24),
      blocklistsz(0),
      blocks(nullptr),
      workingrequest(nullptr),
      workerthreads(new std::thread[this->numthreads]),
      workersem(new Semaphore[this->numthreads]),
      datasem(0),
      mainsem(0),
      channels(new clHCA::stChannel[0x10 * this->numthreads]),
      wavebuffer(new float[0x10 * 0x80 * this->numthreads]),
      shutdown(false),
      stopcurrent(false)
{
    for (unsigned int i = 0; i < this->numthreads; ++i)
    {
        workerthreads[i] = std::thread(&HCADecodeService::Decode_Thread, this, i);
    }
    dispatchthread = std::thread(&HCADecodeService::Main_Thread, this);
}

HCADecodeService::~HCADecodeService()
{
    shutdown = true;
    datasem.notify();
    dispatchthread.join();
    delete[] wavebuffer;
    delete[] channels;
    delete[] workersem;
    delete[] workerthreads;
    delete[] blocks;
}

void HCADecodeService::cancel_decode(void *ptr)
{
    if (!ptr)
    {
        return;
    }
    if (workingrequest == ptr)
    {
        stopcurrent = true;
    }
    else
    {
        std::unique_lock<std::mutex> filelistlock(filelistmtx);

        auto it = filelist.find(ptr);
        if (it != filelist.end())
        {
            filelist.erase(it);
            datasem.wait();
        }
    }
}

void HCADecodeService::wait_on_request(void *ptr)
{
    if (!ptr)
    {
        return;
    }
    else
    {
        while (true)
        {
			if (workingrequest == ptr)
			{
				std::unique_lock<std::mutex> lck(workingmtx);
				workingcv.wait(lck);
				break;
			}
            filelistmtx.lock();
            auto it = filelist.find(ptr);
            if (it != filelist.end())
            {
                filelistmtx.unlock();
                std::unique_lock<std::mutex> lck(workingmtx);
				workingcv.wait(lck);
            }
            else
            {

                filelistmtx.unlock();
                break;
            }
        }
    }
}

void HCADecodeService::wait_for_finish()
{
    filelistmtx.lock();
    while (!filelist.empty() || workingrequest)
    {
        filelistmtx.unlock();
		std::unique_lock<std::mutex> lck(workingmtx);
		workingcv.wait(lck);
        filelistmtx.lock();
    }
    filelistmtx.unlock();
}

std::pair<void *, size_t> HCADecodeService::decode(const char *hcafilename, unsigned int decodefromsample, unsigned int ciphKey1, unsigned int ciphKey2, unsigned int subKey, float volume, int mode, int loop)
{
    clHCA hca(ciphKey1, ciphKey2, subKey);
    void *wavptr = nullptr;
    size_t sz = 0;
    hca.Analyze(wavptr, sz, hcafilename, volume, mode, loop);
    if (wavptr)
    {
        unsigned int decodefromblock = decodefromsample / hca.get_channelCount() >> 10;
        if (decodefromblock >= hca.get_blockCount())
        {
            decodefromblock = 0;
        }
        filelistmtx.lock();
        filelist[wavptr].first = std::move(hca);
        filelist[wavptr].second = decodefromblock;
        filelistmtx.unlock();
        datasem.notify();
    }
    return std::pair<void *, size_t>(wavptr, sz);
}

void HCADecodeService::Main_Thread()
{
    while (true)
    {
        datasem.wait();

		std::unique_lock<std::mutex> lck(workingmtx);
        if (shutdown)
        {
            break;
        }

        filelistmtx.lock();

        load_next_request();
        filelistmtx.unlock();

        numchannels = workingfile.get_channelCount();

        populate_block_list();

        currindex = 0;

        for (unsigned int i = 0; i < numthreads; ++i)
        {
            workersem[i].notify();
        }

        mainsem.wait(numthreads);

        workingrequest = nullptr;

		workingcv.notify_all();
    }

    join_workers();
}

void HCADecodeService::Decode_Thread(unsigned int id)
{
    while (!shutdown)
    {
        workersem[id].wait();
        unsigned int offset = id * numchannels;
        workingfile.PrepDecode(channels + offset);
        unsigned int bindex = currindex++;
        while (bindex < blocklistsz)
        {
            workingfile.AsyncDecode(channels + offset, wavebuffer + (offset << 7), blocks[bindex], workingrequest, chunksize, stopcurrent);
            bindex = currindex++;
        }
        mainsem.notify();
    }
}

void HCADecodeService::load_next_request()
{
    auto it = filelist.begin();
    workingrequest = it->first;
    workingfile = std::move(it->second.first);
    startingblock = it->second.second;
    filelist.erase(it);
    stopcurrent = false;
}

void HCADecodeService::populate_block_list()
{
    unsigned int blockCount = workingfile.get_blockCount();
    unsigned int sz = blockCount / chunksize + (blockCount % chunksize != 0);
    if (sz > blocklistsz)
    {
        delete[] blocks;
        blocks = new unsigned int[sz];
        blocklistsz = sz;
    }
    unsigned int lim = sz * chunksize + startingblock;
    for (unsigned int i = (startingblock / chunksize) * chunksize, j = 0; i < lim; i += chunksize, ++j)
    {
        blocks[j] = i % blockCount;
    }
}

void HCADecodeService::join_workers()
{
    for (unsigned int i = 0; i < numthreads; ++i)
    {
        workersem[i].notify();
        workerthreads[i].join();
    }
}