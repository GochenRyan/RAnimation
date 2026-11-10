#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace RAnimation
{
    // Bump allocator for per-frame scratch (poses produced while walking a graph). Allocation is a pointer
    // increment; Reset() at the start of each evaluation returns everything at once. Blocks are kept between
    // frames so steady-state evaluation never touches the heap. No destructors are run: only trivially
    // destructible types may live here (Transform arrays qualify).
    class MemStack
    {
    public:
        static constexpr size_t kBlockBytes = 64 * 1024;

        template <typename T>
        T* Alloc(size_t count)
        {
            static_assert(std::is_trivially_destructible_v<T>, "MemStack only holds trivially destructible types");
            const size_t bytes = count * sizeof(T);
            return static_cast<T*>(AllocBytes(bytes, alignof(T)));
        }

        void* AllocBytes(size_t bytes, size_t alignment)
        {
            if (bytes == 0)
            {
                return nullptr;
            }
            for (;;)
            {
                if (mCurrent < mBlocks.size())
                {
                    Block& b = mBlocks[mCurrent];
                    const uintptr_t base = reinterpret_cast<uintptr_t>(b.data.get());
                    const uintptr_t aligned = (base + b.used + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
                    const size_t end = static_cast<size_t>(aligned - base) + bytes;
                    if (end <= b.size)
                    {
                        b.used = end;
                        mHighWater = mHighWater > TotalUsed() ? mHighWater : TotalUsed();
                        return reinterpret_cast<void*>(aligned);
                    }
                    ++mCurrent;
                    continue;
                }
                Block b;
                b.size = bytes + alignment > kBlockBytes ? bytes + alignment : kBlockBytes;
                b.data.reset(new uint8_t[b.size]);
                mBlocks.emplace_back(std::move(b));
            }
        }

        void Reset()
        {
            for (Block& b : mBlocks)
            {
                b.used = 0;
            }
            mCurrent = 0;
        }

        size_t TotalUsed() const
        {
            size_t used = 0;
            for (const Block& b : mBlocks)
            {
                used += b.used;
            }
            return used;
        }

        size_t HighWaterBytes() const { return mHighWater; }

    private:
        struct Block
        {
            std::unique_ptr<uint8_t[]> data;
            size_t size = 0;
            size_t used = 0;
        };

        std::vector<Block> mBlocks;
        size_t mCurrent = 0;
        size_t mHighWater = 0;
    };
} // namespace RAnimation
