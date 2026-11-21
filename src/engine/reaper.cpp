#include "reaper.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace vthost {
namespace {

class Reaper {
public:
    static Reaper& instance() {
        static Reaper r;
        return r;
    }
    void add(std::shared_ptr<void> p) {
        {
            std::lock_guard lock(m_);
            items_.push_back(std::move(p));
        }
        cv_.notify_one();
    }
    ~Reaper() {
        {
            std::lock_guard lock(m_);
            quit_ = true;
        }
        cv_.notify_one();
        if (t_.joinable()) t_.join();
    }

private:
    Reaper() : t_([this] { loop(); }) {}
    void loop() {
        for (;;) {
            std::shared_ptr<void> p;
            {
                std::unique_lock lock(m_);
                cv_.wait(lock, [&] { return quit_ || !items_.empty(); });
                if (items_.empty()) return;
                p = std::move(items_.front());
                items_.pop_front();
            }
            try {
                p.reset(); // destrutor (join da thread) fora do render loop
            } catch (...) {
            }
        }
    }
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<void>> items_;
    bool quit_ = false;
    std::thread t_;
};

} // namespace

void retireLater(std::shared_ptr<void> object) {
    if (object) Reaper::instance().add(std::move(object));
}

} // namespace vthost
