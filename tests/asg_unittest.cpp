#include "base/asg_types.h"
#include "base/ring_buffer.h"
#include "base/FunctorThread.h"
#include "base/MessageChannel.h"

#include "client/asg_ring_stream_client.h"
#include "server/asg_ring_stream_server.h"

#include <gtest/gtest.h>
#include <inttypes.h>

#include <functional>
#include <random>
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
                read += serverStream.read(readBuf.data() + read, wanted - read);
            }
            fprintf(stderr, "%s: received packet %u\n", __func__, i);
        }
    });

    serverTestThread.start();
    clientTestThread.start();

    clientTestThread.wait();
    serverTestThread.wait();
}

TEST(ASG, BasicRoundTrip) {
    static constexpr size_t kRingXferSize = 16384;
    static constexpr size_t kRingStepSize = 4096;
    static constexpr size_t kRoundTrips = 1024;
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
        std::vector<uint8_t> readBuf(kSendSizeBytes);
        for (uint32_t i = 0; i < kRoundTrips; ++i) {
            auto buf = clientStream.alloc(kSendSizeBytes);
            memset(buf, 0xff, kSendSizeBytes);
            fprintf(stderr, "%s: sent packet %d\n", __func__, i);
            clientStream.readback(readBuf.data(), kSendSizeBytes);
            fprintf(stderr, "%s: read back packet %d\n", __func__, i);
        }
    });

    FunctorThread serverTestThread([&serverStream]() {
        std::vector<uint8_t> readBuf(kSendSizeBytes);
        for (uint32_t i = 0; i < kRoundTrips; ++i) {
            size_t wanted = kSendSizeBytes;
            size_t read = 0;
            while (read < wanted) {
                read += serverStream.read(readBuf.data() + read, wanted - read);
            }
            fprintf(stderr, "%s: received packet %u\n", __func__, i);
            serverStream.writeFully(readBuf.data(), kSendSizeBytes);
            fprintf(stderr, "%s: wrote back packet %u\n", __func__, i);
        }
    });

    serverTestThread.start();
    clientTestThread.start();

    clientTestThread.wait();
    serverTestThread.wait();
}

TEST(ASG, RandomTraffic) {
    static constexpr size_t kRingXferSize = 16384;
    static constexpr size_t kRingStepSize = 4096;
    static constexpr size_t kTraffics = 1024;

    enum TrafficType {
        Write = 0,
        Read = 1,
    };

    struct Traffic {
        TrafficType type;
        uint32_t size;
        uint8_t byteVal;
    };

    std::vector<Traffic> traffics;

    std::default_random_engine gen;
    gen.seed(0);
    std::bernoulli_distribution readProbDist(0.01);
    std::uniform_int_distribution<uint32_t> trafficSizeDist(1, 8190);
    std::uniform_int_distribution<uint8_t> byteValDist(0, 255);

    for (uint32_t i = 0; i < kTraffics; ++i) {
        TrafficType type = readProbDist(gen) ? TrafficType::Read : TrafficType::Write;
        uint32_t size = trafficSizeDist(gen);
        uint8_t byteVal = byteValDist(gen);
        traffics.push_back({ type, size, byteVal });
    }

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
        doorbellChannel.trySend(0);
    };

    auto unavailRead = [&doorbellChannel, &stop]() {
        int item;
        doorbellChannel.receive(&item);
        if (stop) return -1;
        return 0;
    };

    asg::client::RingStream clientStream(sharedBufPtr, kRingXferSize, doorbell);
    asg::server::RingStream serverStream(sharedBufPtr, kRingXferSize, unavailRead);

    FunctorThread clientTestThread([&clientStream, &traffics]() {
        std::vector<uint8_t> readBuf;
        std::vector<uint8_t> expectedReadBuf;
        uint8_t* sendBuf;
        for (uint32_t i = 0; i < kTraffics; ++i) {
            switch (traffics[i].type) {
            case TrafficType::Write:
                sendBuf = clientStream.alloc(traffics[i].size);
                memset(sendBuf, traffics[i].byteVal, traffics[i].size);
                break;
            case TrafficType::Read:
                readBuf.resize(traffics[i].size);
                expectedReadBuf.resize(traffics[i].size);
                memset(expectedReadBuf.data(), traffics[i].byteVal, traffics[i].size);
                clientStream.readback(readBuf.data(), traffics[i].size);
                EXPECT_EQ(0, memcmp(readBuf.data(), expectedReadBuf.data(), traffics[i].size));
                break;
            }
        }
        clientStream.flush();
    });

    FunctorThread serverTestThread([&serverStream, &traffics]() {
        std::vector<uint8_t> writeBuf;
        std::vector<uint8_t> readBuf;
        std::vector<uint8_t> expectedReadBuf;
        for (uint32_t i = 0; i < traffics.size(); ++i) {
            size_t wanted, read, writeSize;
            switch (traffics[i].type) {
            case TrafficType::Write:
                wanted = traffics[i].size;
                readBuf.resize(wanted);
                expectedReadBuf.resize(wanted);
                memset(expectedReadBuf.data(), traffics[i].byteVal, wanted);
                read = 0;
                while (read < wanted) {
                    read += serverStream.read(readBuf.data() + read, wanted - read);
                }
                EXPECT_EQ(0, memcmp(readBuf.data(), expectedReadBuf.data(), wanted));
                break;
            case TrafficType::Read:
                writeSize = traffics[i].size;
                writeBuf.resize(writeSize);
                memset(writeBuf.data(), traffics[i].byteVal, writeSize);
                serverStream.writeFully(writeBuf.data(), traffics[i].size);
                break;
            }
        }
    });

    serverTestThread.start();
    clientTestThread.start();

    clientTestThread.wait();
    serverTestThread.wait();
}
