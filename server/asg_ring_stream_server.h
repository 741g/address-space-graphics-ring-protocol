// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include "base/asg_types.h"
#include "base/ring_buffer.h"
#include "base/SmallVector.h"
#include "server/server_iostream.h"

#include <functional>
#include <vector>

namespace emugl {

// An IOStream instance that can be used to consume according to asg protocol.
// Takes consumer callbacks as argument.
class RingStream final : public IOStream {
public:
    using OnUnavailableReadCallback = std::function<int()>;
    using GetPtrAndSizeCallback =
        std::function<void(uint64_t, char**, size_t*)>;
    using Buffer =
        android::base::SmallFixedVector<unsigned char, 512>;

    RingStream(
        uint8_t* shared_buffer,
        size_t ring_xfer_buffer_size,
        android::emulation::asg::ConsumerCallbacks callbacks);
    ~RingStream();

    int getNeededFreeTailSize() const;

    int writeFully(const void* buf, size_t len) override;
    const unsigned char *readFully( void *buf, size_t len) override;

    void printStats();

    void pausePreSnapshot() {
        mInSnapshotOperation = true;
    }

    void resume() {
        mInSnapshotOperation = false;
    }

    bool inSnapshotOperation() const {
        return mInSnapshotOperation;
    }

protected:
    virtual void* allocBuffer(size_t minSize) override final;
    virtual int commitBuffer(size_t size) override final;
    virtual const unsigned char* readRaw(void* buf, size_t* inout_len) override final;
    virtual void* getDmaForReading(uint64_t guest_paddr) override final;
    virtual void unlockDma(uint64_t guest_paddr) override final;

    void type1Read(uint32_t available, char* begin, size_t* count, char** current, const char* ptrEnd);
    void type2Read(uint32_t available, size_t* count, char** current, const char* ptrEnd);
    void type3Read(uint32_t available, size_t* count, char** current, const char* ptrEnd);

    struct asg_context mContext;
    android::emulation::asg::ConsumerCallbacks mCallbacks;

    std::vector<asg_type1_xfer> mType1Xfers;
    std::vector<asg_type2_xfer> mType2Xfers;

    Buffer mReadBuffer;
    Buffer mWriteBuffer;
    size_t mReadBufferLeft = 0;

    size_t mXmits = 0;
    size_t mTotalRecv = 0;
    bool mBenchmarkEnabled = false;
    bool mShouldExit = false;
    bool mShouldExitForSnapshot = false;
    bool mInSnapshotOperation = false;
};

}  // namespace emugl
