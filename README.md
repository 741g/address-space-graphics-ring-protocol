# Address Space Graphics Ring Stream Protocol

This library captures the ring read/write protocol behind Address Space Graphics in gfxstream (https://android.googlesource.com/device/generic/vulkan-cereal), so it can be used with a wide range of protocols and virtual devices.

It consists of a client and server side, to be statically or dynamically linked with the guest and host side, respectively. This library is NOT responsible for:

- Creating the shared memory regions in the first place, which is for virtio-gpu or some other driver to handle.
- Specifying what commands get sent to/from the server, which is the responsibility of the guest frontend / the host renderer.

The client has the following inputs:

1. A shared memory region that is at least XX pages aligned to the max of guest and host page alignment.
2. A function pointer to call to doorbell the server.

The server has the following inputs:

1. A shared memory region that is XX pages algined to the max of guest and host page alignment.
2. A callback that is called when there is no traffic on the shared region for a while.

The shared region has the following layout:

    | struct asg_ring_storage | N bytes for the xfer buffer |

`asg_ring_storage` objects consist of ring buffers with consumed/available counters shared between guest and host, along with items. It is followed with an actual buffer over which transfers take place. `N` is also known as the `ringXferBufferSize` and is where the guest places actual data. Descriptors of the data (like in vq's but not in kernel space :)) are the actual items consumed in `asg_ring_storage`.

# TODO

The code right now is quite roughly extracted from https://android.googlesource.com/device/generic/goldfish-opengl and https://android.googlesource.com/device/generic/vulkan-cereal, and needs:

- Performance tests
- Maybe provide some plausible host IPC implementations of doorbell callbacks and shared memory?

# How to use

See unit tests in `test/asg_unittest.cpp` for more details. A walkthrough of the initialization in the `Basic` test:

## Define constants and initialize shared buffer with ring fields

First, we define constants that say how large the xfer buffer is going to be:

```
    static constexpr size_t kRingXferSize = 16384;
```

The total size occupied by the shared buffer will be `sizeof(struct asg_ring_storage + kRingXferSize`. Next, we define a step size for flushing buffers on the ring. It must be a power of 2 that is less than half `kRingXferSize`:

```
    static constexpr size_t kRingStepSize = 4096;
```

Then, the shared buffer is initialized accordingly:

```
    std::vector<uint8_t> sharedBuf(sizeof(struct asg_ring_storage) + kRingXferSize, 0);
    uint8_t* sharedBufPtr = sharedBuf.data();

    struct asg_context context =
        asg_context_create((char*)sharedBufPtr, (char*)sharedBufPtr + sizeof(struct asg_ring_storage), kRingXferSize);

    context.ring_config->buffer_size = kRingXferSize;
    context.ring_config->flush_interval = kRingStepSize;
    context.ring_config->host_consumed_pos = 0;
    context.ring_config->transfer_mode = 1;
    context.ring_config->in_error = 0;
```

`asg_context_create()` along with the following `ring_config` field settings populate the `asg_ring_storage` section of the shared memory with the correct struct layout and other parameters, and also initializes fields for the ring buffers themselves.

```
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
```

## Define doorbell callbacks on the client and server

ASG requires a mechanism for the client to wake the server, and for the server to receive such wakeup requests, on a channel that isn't necessarily reliant on shared memory. There are two sides to this:
    - Client: doorbell callback called whenever the client, according to AGS ring protocol, thinks it might be a good idea to wake up the server.
    - Server: unavailable read callback called when we want to stop listening for traffic from the client and go to sleep inside this callback. It is assumed the client doorbell callback will trigger stuff that wakes up the server from the point in the callback where it's sleeping.

Note that these callbacks can be no-ops if the server spins forever. But usually we don't want that, so we have the server go to sleep in its unavailable-read callback.

```
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
```

In the unit tests, we implement this with a message channel. The server side blocks in `MessageChannel::receive`, to be woke up by the `doorbell` function defined above it.

## Initialize client/server RingStreams and start sending traffic

```
    asg::client::RingStream clientStream(sharedBufPtr, kRingXferSize, doorbell);
    asg::server::RingStream serverStream(sharedBufPtr, kRingXferSize, unavailRead);
```

After initializing, we can then send/receive traffic. In the test, it's realized as starting two threads, one for client and one for server, and sending traffic like so:

```
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
```

Note the use of `alloc` only, instead of always `alloc` followed by `flush`. This can save on doorbells, but even if we always follow up flush from alloc, it's possible to still avoid doorbells in that case.

The tests contain further code that demonstrates sending replies. There is also a random test to test correctness more thoroughly.
