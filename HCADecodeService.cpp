#include <map>
#include "HCADecodeService.h"
#include "clHCA.h"

HCADecodeService::HCADecodeService()
    : numthreads{ std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 1 },
      mainsem{ this->numthreads },
      wavoutsem{ this->numthreads },
      workingblocks{ new int[this->numthreads] },
      worker_threads{ new std::thread[this->numthreads] },
      workersem{ new Semaphore[this->numthreads]{} },
      channels{ new clHCA::stChannel[0x10 * this->numthreads] },
      chunksize{ 16 },
      datasem{ 0 },
      numchannels{ 0 },
	  requestnum{ 0 },
      workingrequest{ nullptr },
      shutdown{ false },
	  stopcurrent{ false }
{
    for (unsigned int i = 0; i < this->numthreads; ++i)
    {
        worker_threads[i] = std::thread{ &HCADecodeService::Decode_Thread, this, i };
        workingblocks[i] = -1;
    }
    dispatchthread = std::thread{ &HCADecodeService::Main_Thread, this };
}

HCADecodeService::HCADecodeService(unsigned int numthreads, unsigned int chunksize)
    : numthreads{ numthreads ? numthreads : (std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 1) },
      mainsem{ this->numthreads },
      wavoutsem{ this->numthreads },
      workingblocks{ new int[this->numthreads] },
      worker_threads{ new std::thread[this->numthreads] },
      workersem{ new Semaphore[this->numthreads]{} },
      channels{ new clHCA::stChannel[0x10 * this->numthreads] },
      chunksize{ chunksize ? chunksize : 16 },
      datasem{ 0 },
      numchannels{ 0 },
	  requestnum{ 0 },
      workingrequest{ nullptr },
      shutdown{ false },
	  stopcurrent{ false }
{
    for (unsigned int i = 0; i < this->numthreads; ++i)
    {
        worker_threads[i] = std::thread{ &HCADecodeService::Decode_Thread, this, i };
        workingblocks[i] = -1;
    }
    dispatchthread = std::thread{ &HCADecodeService::Main_Thread, this };
}

HCADecodeService::~HCADecodeService()
{
    shutdown = true;
    datasem.notify();
    dispatchthread.join();
    delete[] channels;
    delete[] workersem;
    delete[] worker_threads;
}

void HCADecodeService::cancel_decode(void* ptr)
{
    if (ptr == nullptr)
    {
        return;
    }
	if (workingrequest == ptr)
	{
		stopcurrent = true;
		wait_on_all_threads(wavoutsem);
	}
	else
    {
        std::unique_lock<std::mutex> filelistlock(filelistmtx);

        auto it = requesttoorder.find(ptr);
        if (it != requesttoorder.end())
        {
			auto it2 = filelist.find(requesttoorder[ptr]);
			requesttoorder.erase(it);
			filelist.erase(it2);
            datasem.wait();
        }
    }
}

void HCADecodeService::wait_on_request(void* ptr)
{
    if (ptr == nullptr)
    {
        return;
    }
	if (workingrequest == ptr)
	{
		workingmtx.lock();
		workingmtx.unlock();
	}
	else
	{
		while (true)
		{
			filelistmtx.lock();
			auto it = requesttoorder.find(ptr);
			if (it != requesttoorder.end())
			{
				filelistmtx.unlock();
				workingmtx.lock();
				workingmtx.unlock();
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
    while(!filelist.empty() || workingrequest)
    {
        filelistmtx.unlock();
        workingmtx.lock();
        workingmtx.unlock();
        filelistmtx.lock();
    }
    filelistmtx.unlock();
}

std::pair<void*, size_t> HCADecodeService::decode(const char* hcafilename, unsigned int decodefromsample, unsigned int ciphKey1, unsigned int ciphKey2, float volume, int mode, int loop)
{
    clHCA hca(ciphKey1, ciphKey2);
    void* wavptr = nullptr;
    size_t sz = 0;
    hca.Analyze(wavptr, sz, hcafilename, volume, mode, loop);
    if (wavptr != nullptr)
    {
        unsigned int decodefromblock = decodefromsample / (hca.get_channelCount() << 10);
        if (decodefromblock > hca.get_blockCount())
        {
            decodefromblock = 0;
        }
        filelistmtx.lock();
		requesttoorder[wavptr] = requestnum;
        filelist[requestnum].first = std::move(hca);
        filelist[requestnum].second = decodefromblock;
		++requestnum;
        filelistmtx.unlock();
        datasem.notify();
    }
    return std::pair<void*, size_t>(wavptr, sz);
}

void HCADecodeService::Main_Thread()
{
    while (true)
    {
        datasem.wait();

        if (shutdown)
        {
            break;
        }

        filelistmtx.lock();
        workingmtx.lock();
        load_next_request();
        filelistmtx.unlock();

        numchannels = workingfile.get_channelCount();
        workingfile.PrepDecode(channels, numthreads);

        populate_block_list();

        while(!blocks.empty() && workingrequest)
        {
            mainsem.wait();
            for (unsigned int i = 0; i < numthreads; ++i)
            {
                if (blocks.empty())
                {
                    mainsem.notify();
                    goto OUT;
                }
                if (workingblocks[i] == -1)
                {
                    workingblocks[i] = blocks.front();
                    blocks.pop_front();
                    workersem[i].notify();
                    mainsem.wait();
                }
            }
            mainsem.notify();
        }
    
    OUT:
        wait_on_all_threads(mainsem);
        workingrequest = nullptr;

        workingmtx.unlock();
    }

    join_workers();
}

void HCADecodeService::Decode_Thread(int id)
{
    workersem[id].wait();
    while (workingblocks[id] != -1)
    {
		wavoutsem.wait();
        workingfile.AsyncDecode(channels + (id * numchannels), workingblocks[id], workingrequest, chunksize, stopcurrent);
		wavoutsem.notify();
        workingblocks[id] = -1;
        mainsem.notify();
        workersem[id].wait();
    }
}

void HCADecodeService::load_next_request()
{
	auto it = requesttoorder.begin();
	workingrequest = it->first;
	unsigned int req = it->second;
	requesttoorder.erase(it);
    auto it2 = filelist.find(req);
    workingfile = std::move(it2->second.first);
    startingblock = it2->second.second;
    filelist.erase(it2);
	stopcurrent = false;
}

void HCADecodeService::populate_block_list()
{
    blocks.clear();
    unsigned int blockCount = workingfile.get_blockCount();
    int sz = blockCount / chunksize + (blockCount % chunksize != 0);
    unsigned int lim = sz * chunksize + startingblock;
    for (unsigned int i = (startingblock / chunksize) * chunksize; i < lim; i += chunksize)
    {
        blocks.push_back(i % blockCount);
    }
}

void HCADecodeService::wait_on_all_threads(Semaphore& sem)
{
	sem.wait(numthreads);
	sem.notify(numthreads);
}

void HCADecodeService::join_workers()
{
    for (unsigned int i = 0; i < numthreads; ++i)
    {
        workersem[i].notify();
        worker_threads[i].join();
    }
}
