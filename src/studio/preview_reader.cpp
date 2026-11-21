#include "preview_reader.h"

#include "vthost/core/preview.h"

#include <atomic>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace vthost;

PreviewReader::PreviewReader(int slot, QObject* parent) : QObject(parent), slot_(slot) {
    timer_.setInterval(40);
    connect(&timer_, &QTimer::timeout, this, &PreviewReader::poll);
}

PreviewReader::~PreviewReader() {
#ifdef Q_OS_WIN
    if (block_) UnmapViewOfFile(block_);
    if (mapping_) CloseHandle(static_cast<HANDLE>(mapping_));
#endif
}

void PreviewReader::setActive(bool active) {
    if (active) {
        lastFrameNumber_ = 0; // mostra logo o que lá estiver
        timer_.start();
    } else {
        timer_.stop();
    }
}

bool PreviewReader::openMapping() {
#ifdef Q_OS_WIN
    if (block_) return true;
    if (lastOpenAttempt_.isValid() && lastOpenAttempt_.elapsed() < 1000) return false;
    lastOpenAttempt_.start();
    const wchar_t* name = slot_ == 2 ? kPreviewName3 : slot_ == 1 ? kPreviewName2 : kPreviewName;
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!h) return false;
    const void* view = MapViewOfFile(h, FILE_MAP_READ, 0, 0, kPreviewBlockBytes);
    if (!view) {
        CloseHandle(h);
        return false;
    }
    mapping_ = h;
    block_ = static_cast<const unsigned char*>(view);
    return true;
#else
    return false;
#endif
}

void PreviewReader::poll() {
    bool fresh = false;
    if (openMapping()) {
        const auto* hdr = reinterpret_cast<const PreviewHeader*>(block_);
        const auto* seqPtr = reinterpret_cast<const volatile std::uint64_t*>(&hdr->sequence);
        for (int attempt = 0; attempt < 3; ++attempt) {
            const std::uint64_t s1 = *seqPtr;
            std::atomic_thread_fence(std::memory_order_acquire);
            if (s1 & 1u || hdr->magic != kPreviewMagic) continue;
            const std::uint64_t frameNo = hdr->frameNumber;
            if (frameNo == lastFrameNumber_) break;
            const int w = static_cast<int>(hdr->width);
            const int h = static_cast<int>(hdr->height);
            if (w <= 0 || h <= 0 || w > kPreviewMaxWidth || h > kPreviewMaxHeight) break;
            QImage img(w, h, QImage::Format_RGB32);
            const unsigned char* src = block_ + sizeof(PreviewHeader);
            const int row = w * 4;
            for (int y = 0; y < h; ++y) std::memcpy(img.scanLine(y), src + static_cast<std::size_t>(y) * row, static_cast<std::size_t>(row));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (*seqPtr != s1) continue;
            frame_ = std::move(img);
            lastFrameNumber_ = frameNo;
            lastFrame_.start();
            fresh = true;
            break;
        }
    }
    const bool isLive = live();
    if (fresh || isLive != wasLive_) emit frameUpdated();
    wasLive_ = isLive;
}
