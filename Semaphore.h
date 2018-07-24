#pragma once
#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <mutex>
#include <condition_variable>

class Semaphore
{
public:
    Semaphore(int count_ = 0) : count(count_) {}
    Semaphore(unsigned int count_) : count(static_cast<int>(count_)) {}

    inline void notify(int n = 1)
    {
        std::unique_lock<std::mutex> lock(mtx);
        count += n;
		for(int i = 0; i < n; ++i) { cv.notify_one(); }
    }

    inline void wait(int n = 1)
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock,[this, n] {
			bool wake = count >= n;
			if (!wake)
			{
				cv.notify_one();
			}
			return wake;
		});
        count -= n;
    }

private:
    std::mutex mtx;
    std::condition_variable cv;
    int count;
};

#endif //SEMAPHORE_H
