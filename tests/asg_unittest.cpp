#include "base/asg_types.h"
#include "base/ring_buffer.h"
#include "base/FunctorThread.h"
#include "base/MessageChannel.h"

#include "client/asg_ring_stream_client.h"
#include "server/asg_ring_stream_server.h"

#include <gtest/gtest.h>
#include <inttypes.h>

#include <functional>
#include <vector>

using android::base::MessageChannel;
using android::base::FunctorThread;

TEST(ASG, Basic) {
    static constexpr size_t kRingXferSize = 16384;
    static constexpr size_t kRingStepSize = 4096;
    static constexpr size_t kSends = 1024;
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

    auto doorbell = [&doorbellChannel]() {
        fprintf(stderr, "%s: doorbell\n", __func__);
        doorbellChannel.trySend(0);
    };

    auto unavailRead = [&doorbellChannel, &stop]() {
        int item;
        fprintf(stderr, "%s: unavailable, wait\n", __func__);
        doorbellChannel.receive(&item);
        fprintf(stderr, "%s: unavailable, got a doorbell\n", __func__);
        if (stop) return -1;
        return 0;
    };

    asg::client::RingStream clientStream(sharedBufPtr, kRingXferSize, doorbell);
    asg::server::RingStream serverStream(sharedBufPtr, kRingXferSize, unavailRead);

    FunctorThread clientTestThread([&clientStream]() {
        for (uint32_t i = 0; i < kSends; ++i) {
            auto buf = clientStream.alloc(kSendSizeBytes);
            memset(buf, 0xff, kSendSizeBytes);
            fprintf(stderr, "%s: sent packet %d\n", __func__, i);
        }
        clientStream.flush();
    });

    FunctorThread serverTestThread([&serverStream]() {
        std::vector<uint8_t> readBuf(kSendSizeBytes);
        for (uint32_t i = 0; i < kSends; ++i) {
            size_t wanted = kSendSizeBytes;
            size_t read = 0;
            while (read < wanted) {
                read = serverStream.read(readBuf.data(), wanted - read);
            }
            fprintf(stderr, "%s: received packet %u\n", __func__, i);
        }
    });

    serverTestThread.start();
    clientTestThread.start();

    clientTestThread.wait();
    serverTestThread.wait();
}
