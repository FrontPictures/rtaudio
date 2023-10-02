#pragma once
#include <mutex>
#include <algorithm>
#include <cassert>
#include <algorithm>
#include <optional>
#include <cstring>

template<class T> class RingBuffer
{
private:
    T *data = nullptr;
    size_t buflen = 0;
    size_t readPos = 0, writePos = 0, readLevel = 0, writeLevel = 0;
    bool allow_overread = false;
    bool alwaysReadLatest = false;

public:
    RingBuffer(size_t size = 0, bool allow_overread = false, bool alwaysReadLatest = false)
        : buflen(size), allow_overread(allow_overread), alwaysReadLatest(alwaysReadLatest)
    {
        if (buflen != 0) {
            data = new T[buflen];
            memset(data, 0, sizeof(T) * buflen);
        }
        if (alwaysReadLatest) {
            writeSilence(size);
        }
    }
    ~RingBuffer()
    {
        if (data) {
            delete[] data;
        }
    }
    RingBuffer(const RingBuffer &other)
        : buflen(other.buflen), readPos(other.readPos), writePos(other.writePos),
          readLevel(other.readLevel), writeLevel(other.writeLevel)
    { //copy constructor
        if (buflen) {
            data = new T[buflen];
            memcpy(data, other.data, buflen * sizeof(T));
        }
    }
    RingBuffer(RingBuffer &&other) noexcept
    { //move constructor
        swap(*this, other);
    }
    RingBuffer &operator=(RingBuffer other) noexcept
    { //move
        swap(*this, other);
        return *this;
    }

    friend void swap(RingBuffer &o1, RingBuffer &o2) noexcept
    {
        using std::swap;
        swap(o1.data, o2.data);
        swap(o1.buflen, o2.buflen);
        swap(o1.readPos, o2.readPos);
        swap(o1.writePos, o2.writePos);
        swap(o1.readLevel, o2.readLevel);
        swap(o1.writeLevel, o2.writeLevel);
        swap(o1.allow_overread, o2.allow_overread);
        swap(o1.alwaysReadLatest, o2.alwaysReadLatest);
    }

    void resize(int samples)
    {
        T *newData = nullptr;
        int copySize = GetReadDataAvailable();
        if (samples) {
            newData = new T[samples];
            if (copySize > 0) {
                if (samples < copySize) {
                    ReadSkip(copySize - samples);
                    copySize = samples;
                }
                Read(newData, copySize);
            }
        } else {
            copySize = 0;
        }
        if (data) {
            delete[] data;
        }
        data = newData;
        buflen = samples;
        readPos = 0;
        writePos = copySize;
        readLevel = 0;
        writeLevel = 0;
    }

    void Clear()
    {
        readPos = 0;
        writePos = 0;
        readLevel = 0;
        writeLevel = 0;
    }

    size_t GetReadDataAvailable() const
    {
        if (alwaysReadLatest) {
            return buflen;
        }
        if (!allow_overread)
            return writePos + (writeLevel * buflen) - readPos + (readLevel * buflen);
        return SIZE_MAX;
    }
    size_t GetWriteDataAvailable() const
    {
        if (!alwaysReadLatest)
            return (readPos + ((readLevel + 1) * buflen)) - (writePos + (writeLevel * buflen));
        return SIZE_MAX;
    }
    bool ReadAvailable(size_t data_len) const
    {
        if (GetReadDataAvailable() >= data_len)
            return true;
        return false;
    }
    bool WriteAvailable(size_t data_len) const
    {
        if (GetWriteDataAvailable() >= data_len)
            return true;
        return false;
    }

    bool Write(const T *data, size_t data_len)
    {
        assert(WriteAvailable(data_len));
        if (WriteAvailable(data_len) == false) {
            return false;
        }
        int written_size = 0;
        while (written_size != data_len) {
            int write_size = std::min(buflen - writePos, data_len - written_size);
            memcpy(this->data + writePos, data + written_size, write_size * sizeof(T));
            written_size += write_size;
            writePos += write_size;
            if (writePos >= buflen) {
                writePos -= buflen;
                writeLevel++;
            }
        }

        return true;
    }
    bool writeSilence(size_t data_len)
    {
        if (WriteAvailable(data_len) == false) {
            return false;
        }
        int first_size = std::min(buflen - writePos, data_len);
        std::memset(this->data + writePos, 0, first_size * sizeof(T));
        std::memset(this->data, 0, (data_len - first_size) * sizeof(T));
        writePos += data_len;
        if (writePos >= buflen) {
            writePos -= buflen;
            writeLevel++;
        }
        return true;
    }

    bool Read(T *data, size_t data_len)
    {
        assert(!alwaysReadLatest);
        if (alwaysReadLatest) {
            return false;
        }
        if (ReadNoSeek(data, data_len) == false)
            return false;
        ReadSkip(data_len);
        return true;
    }

    bool ReadNoSeek(T *data, size_t data_len)
    {
        int first_size = std::min(buflen - readPos, data_len);
        memcpy(data, this->data + readPos, sizeof(T) * first_size);
        memcpy(data + first_size, this->data, sizeof(T) * (data_len - first_size));
        return true;
    }

    bool ReadLatest(T *data, size_t data_len)
    {
        if (data_len > GetReadDataAvailable())
            return false;
        int tReadPos = writePos - data_len;
        if (tReadPos < 0) {
            tReadPos += buflen;
        }
        int first_size = std::min(buflen - tReadPos, data_len);
        memcpy(data, this->data + tReadPos, sizeof(T) * first_size);
        memcpy(data + first_size, this->data, sizeof(T) * (data_len - first_size));
        return true;
    }

    bool ReadSkip(size_t data_len)
    {
        assert(ReadAvailable(data_len));
        if (ReadAvailable(data_len) == false) {
            return false;
        }
        readPos += data_len;
        if (readPos >= buflen) {
            readPos -= buflen;
            if (!allow_overread)
                readLevel++;
            if (readLevel == writeLevel) {
                readLevel = 0;
                writeLevel = 0;
            }
        }
        return true;
    }

    size_t GetSize() const { return buflen; }
};
