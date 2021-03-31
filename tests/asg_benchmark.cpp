#include "base/asg_types.h"
#include "base/ring_buffer.h"
#include "base/FunctorThread.h"
#include "base/MessageChannel.h"

#include "client/asg_ring_stream_client.h"
#include "server/asg_ring_stream_server.h"

#include <gtest/gtest.h>
#include <inttypes.h>

#include <chrono>
#include <functional>
#include <random>
#include <vector>

using android::base::MessageChannel;
using android::base::FunctorThread;

// Benchmark that tests how fast we can dump kSends * kSendSizeBytes of data into a sink,
// with kSends packets.
TEST(ASG, BenchmarkBasicSend) {
    static constexpr size_t kRingXferSize = 16384;
    static constexpr size_t kRingStepSize = 4096;
    static constexpr size_t kSends = 1024 * 50;
    static constexpr size_t kSendSizeBytes = 384;

    std::vector<uint8_t> sharedBuf(sizeof(struct asg_ring_storage) + kRingXferSize, 0);
    uint8_t* sharedBufPtr = sharedBuf.data();

    struct asg_context context =
        asg_context_create((char*)sharedBufPtr, (char*)sharedBufPtr + sizeof(struct asg_ring_storage), kRingXferSize);

    context.ring_config->buffer_size = kRingXferSize;
    context.ring_config->flush_interval = kRingStepSize;
    context.ring_config->host_consumed_pos = 0;
    context.ring_config->transfer_mode = 1;
    context.ring_config->in_error = 0;

    MessageChannel<int, 1> doorbellChannel;
    bool stop = false;

    uint32_t doorbells = 0;
    auto doorbell = [&doorbellChannel, &doorbells]() {
        doorbellChannel.trySend(0);
        ++doorbells;
    };

    auto unavailRead = [&doorbellChannel, &stop]() {
        int item;
        doorbellChannel.receive(&item);
        if (stop) return -1;
        return 0;
    };

    asg::client::RingStream clientStream(sharedBufPtr, kRingXferSize, doorbell);
    asg::server::RingStream serverStream(sharedBufPtr, kRingXferSize, unavailRead);

    FunctorThread clientTestThread([&clientStream]() {
        for (uint32_t i = 0; i < kSends; ++i) {
            auto buf = clientStream.alloc(kSendSizeBytes);
            memset(buf, 0xff, kSendSizeBytes);
        }
        clientStream.flush();
    });

    size_t wanted = kSends * kSendSizeBytes;
    std::vector<uint8_t> readBuf(wanted, 0);
    std::vector<uint8_t> golden(wanted, 0xff);
    memset(golden.data(), 0xff, wanted);

    FunctorThread serverTestThread([&serverStream, &readBuf, &golden]() {

        size_t wanted = kSends * kSendSizeBytes;
        size_t read = 0;

        while (read < wanted) {
            size_t readThisTime = serverStream.read(readBuf.data() + read, wanted - read);

            // Do some processing so there's an actual workload to suppress
            // doorbells against.
            //
            // Here just check if we actually read the expected byte values.
            // Doing actual processing here helps suppress doorbells as the
            // client will know for sure it's cool to put more data on the ring
            // without doorbelling (otherwise, we might be in serverStream.read
            // which might end up sleeping in |unavailRead|).
            //
            // In real use cases, we would have been doing even heavier
            // processing here (rendering)

            EXPECT_EQ(0, memcmp(readBuf.data() + read, golden.data() + read, readThisTime));

            read += readThisTime;
        }
    });

    auto start = std::chrono::high_resolution_clock::now();
    serverTestThread.start();
    clientTestThread.start();

    clientTestThread.wait();
    serverTestThread.wait();
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<float> duration = end - start;
    fprintf(stderr, "%s: Sent %zu bytes in %f seconds with %u doorbells and %zu packets. %f MB/s bandwidth, %f Hz doorbells, packet:doorbell ratio %f\n", __func__,
            kSends * kSendSizeBytes,
            duration.count(),
            doorbells,
            kSends,
            ((float)kSends * kSendSizeBytes / 1048576.0) / duration.count(),
            (float)doorbells / duration.count(),
            (float)kSends / (float)doorbells);
}
