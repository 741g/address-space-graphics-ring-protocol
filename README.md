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

The code right now is quite roughly extracted from https://android.googlesource.com/device/generic/goldfish-opengl and https://android.googlesource.com/device/generic/vulkan-cereal, and needs generic tests that aren't gfxstream specific.
