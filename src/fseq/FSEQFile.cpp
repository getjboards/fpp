// This #define must be before any #include's
#define _FILE_OFFSET_BITS 64

#include <vector>
#include <cstring>

#include <zstd.h>

#include <stdio.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "FSEQFile.h"
#include "common.h"
#include "log.h"

inline long roundTo4(long i) {
    long remainder = i % 4;
    if (remainder == 0) {
        return i;
    }
    return i + 4 - remainder;
}

inline uint32_t read4ByteUInt(const uint8_t* data) {
    uint32_t r = (data[0])
        + (data[1] << 8)
        + (data[2] << 16)
        + (data[3] << 24);
    return r;
}
inline uint32_t read3ByteUInt(const uint8_t* data) {
    uint32_t r = (data[0])
        + (data[1] << 8)
        + (data[2] << 16);
    return r;
}
inline uint32_t read2ByteUInt(const uint8_t* data) {
    uint32_t r = (data[0])
        + (data[1] << 8);
    return r;
}
inline void write2ByteUInt(uint8_t* data, uint32_t v) {
    data[0] = (uint8_t)(v & 0xFF);
    data[1] = (uint8_t)((v >> 8) & 0xFF);
}
inline void write3ByteUInt(uint8_t* data, uint32_t v) {
    data[0] = (uint8_t)(v & 0xFF);
    data[1] = (uint8_t)((v >> 8) & 0xFF);
    data[2] = (uint8_t)((v >> 16) & 0xFF);
}
inline void write4ByteUInt(uint8_t* data, uint32_t v) {
    data[0] = (uint8_t)(v & 0xFF);
    data[1] = (uint8_t)((v >> 8) & 0xFF);
    data[2] = (uint8_t)((v >> 16) & 0xFF);
    data[3] = (uint8_t)((v >> 24) & 0xFF);
}

FSEQFile* FSEQFile::openFSEQFile(const std::string &fn) {
    
    FILE *seqFile = fopen((const char *)fn.c_str(), "r");
    if (seqFile == NULL) {
        LogErr(VB_SEQUENCE, "Error opening sequence file: %s. fopen returned NULL\n",
               fn.c_str());
        return nullptr;
    }
    
    fseeko(seqFile, 0L, SEEK_SET);
    unsigned char tmpData[48];
    int bytesRead = fread(tmpData, 1, 48, seqFile);
    posix_fadvise(fileno(seqFile), 0, 0, POSIX_FADV_RANDOM);
    posix_fadvise(fileno(seqFile), 0, 1024*1024, POSIX_FADV_WILLNEED);

    if ((bytesRead < 4)
        || (tmpData[0] != 'P' && tmpData[0] != 'F')
        || tmpData[1] != 'S'
        || tmpData[2] != 'E'
        || tmpData[3] != 'Q') {
        LogErr(VB_SEQUENCE, "Error opening sequence file: %s. Incorrect File Format header: '%s', bytesRead: %d\n",
               fn.c_str(), tmpData, bytesRead);
        HexDump("Sequence File head:", tmpData, bytesRead);
        fclose(seqFile);
        return nullptr;
    }
    
    if (bytesRead < 8) {
        LogErr(VB_SEQUENCE, "Sequence file %s too short, unable to read FSEQ version fields\n", fn.c_str());
        HexDump("Sequence File head:", tmpData, bytesRead);
        fclose(seqFile);
        return nullptr;
    }
    
    int seqVersionMinor = tmpData[6];
    int seqVersionMajor = tmpData[7];
    
    ///////////////////////////////////////////////////////////////////////
    // Get Channel Data Offset
    uint64_t seqChanDataOffset = read2ByteUInt(&tmpData[4]);
    std::vector<uint8_t> header(seqChanDataOffset);
    fseeko(seqFile, 0L, SEEK_SET);
    bytesRead = fread(&header[0], 1, seqChanDataOffset, seqFile);
    if (bytesRead != seqChanDataOffset) {
        LogErr(VB_SEQUENCE, "Error opening sequence file: %s. Could not read header.\n", fn.c_str());
        HexDump("Sequence File head:", &header[0], bytesRead);
        fclose(seqFile);
    }
    

    FSEQFile *file = nullptr;
    if (seqVersionMajor == 1) {
        file = new V1FSEQFile(fn, seqFile, header);
    } else if (seqVersionMajor == 2) {
        file = new V2FSEQFile(fn, seqFile, header);
    } else {
        LogErr(VB_SEQUENCE, "Error opening sequence file: %s. Unknown FSEQ version %d-%d\n",
               fn.c_str(), seqVersionMajor, seqVersionMinor);
        HexDump("Sequence File head:", tmpData, bytesRead);
        fclose(seqFile);
        
        return nullptr;
    }
    
    LogDebug(VB_SEQUENCE, "Sequence File Information\n");
    LogDebug(VB_SEQUENCE, "seqFilename           : %s\n", fn.c_str());
    LogDebug(VB_SEQUENCE, "seqVersion            : %d.%d\n", seqVersionMajor, seqVersionMinor);
    LogDebug(VB_SEQUENCE, "seqFormatID           : %c%c%c%c\n", tmpData[0], tmpData[1], tmpData[2], tmpData[3]);
    LogDebug(VB_SEQUENCE, "seqChanDataOffset     : %d\n", seqChanDataOffset);
    LogDebug(VB_SEQUENCE, "seqChannelCount       : %d\n", file->m_seqChannelCount);
    LogDebug(VB_SEQUENCE, "seqNumPeriods         : %d\n", file->m_seqNumFrames);
    LogDebug(VB_SEQUENCE, "seqStepTime           : %dms\n", file->m_seqStepTime);

    if (seqVersionMajor == 2) {
        //FIXME log v2 stuff
    }
    return file;
}
FSEQFile* FSEQFile::createFSEQFile(const std::string &fn,
                                   int version,
                                   CompressionType ct,
                                   int level) {
    if (version == 1) {
        return new V1FSEQFile(fn);
    }
    return new V2FSEQFile(fn, ct, level);
}



FSEQFile::FSEQFile(const std::string &fn)
    : filename(fn),
    m_seqNumFrames(0),
    m_seqChannelCount(0),
    m_seqStepTime(50),
    m_variableHeaders(),
    m_uniqueId(0),
    m_seqFileSize(0)
{
    m_seqFile = fopen((const char *)fn.c_str(), "w");
}

void FSEQFile::initializeFromFSEQ(const FSEQFile& fseq) {
    m_seqNumFrames = fseq.m_seqNumFrames;
    m_seqChannelCount = fseq.m_seqChannelCount;
    m_seqStepTime = fseq.m_seqStepTime;
    m_variableHeaders = fseq.m_variableHeaders;
    m_uniqueId = fseq.m_uniqueId;
}


FSEQFile::FSEQFile(const std::string &fn, FILE *file, const std::vector<uint8_t> &header)
    : filename(fn), m_seqFile(file), m_uniqueId(0) {
    fseeko(m_seqFile, 0L, SEEK_END);
    m_seqFileSize = ftello(m_seqFile);
    fseeko(m_seqFile, 0L, SEEK_SET);

    m_seqChanDataOffset = read2ByteUInt(&header[4]);
    m_seqVersionMinor = header[6];
    m_seqVersionMajor = header[7];
    m_seqVersion      = (m_seqVersionMajor * 256) + m_seqVersionMinor;
    m_seqChannelCount = read4ByteUInt(&header[10]);
    m_seqNumFrames = read4ByteUInt(&header[14]);
    m_seqStepTime = read2ByteUInt(&header[18]);
}
FSEQFile::~FSEQFile() {
    fclose(m_seqFile);
}
void FSEQFile::parseVariableHeaders(const std::vector<uint8_t> &header, int start) {
    while (start < header.size() - 5) {
        int len = read2ByteUInt(&header[start]);
        if (len) {
            VariableHeader vheader;
            vheader.code[0] = header[start + 2];
            vheader.code[1] = header[start + 3];
            vheader.data.resize(len - 4);
            memcpy(&vheader.data[0], &header[start + 4], len - 4);
            m_variableHeaders.push_back(vheader);
        } else {
            len += 4;
        }
        start += len;
    }
}


V1FSEQFile::V1FSEQFile(const std::string &fn)
  : FSEQFile(fn)
{
}
void V1FSEQFile::writeHeader() {
    static int fixedHeaderLength = 28;
    uint8_t header[28];
    memset(header, 0, 28);
    header[0] = 'P';
    header[1] = 'S';
    header[2] = 'E';
    header[3] = 'Q';

    // data offset
    uint32_t dataOffset = fixedHeaderLength;
    for (auto &a : m_variableHeaders) {
        dataOffset += a.data.size() + 4;
    }
    dataOffset = roundTo4(dataOffset);
    write2ByteUInt(&header[4], dataOffset);
    
    header[6] = 0; //minor
    header[7] = 1; //major
    // Fixed header length
    write2ByteUInt(&header[8], fixedHeaderLength);
    // Step Size
    write4ByteUInt(&header[10], m_seqChannelCount);
    // Number of Steps
    write4ByteUInt(&header[14], m_seqNumFrames);
    // Step time in ms
    write2ByteUInt(&header[18], m_seqStepTime);
    // universe count
    write2ByteUInt(&header[20], 0);
    // universe Size
    write2ByteUInt(&header[22], 0);
    // universe Size
    header[24] = 1;
    // color order
    header[25] = 2;
    header[26] = 0;
    header[27] = 0;
    fwrite(header, 1, 28, m_seqFile);
    for (auto &a : m_variableHeaders) {
        uint8_t buf[4];
        uint32_t len = a.data.size() + 4;
        write2ByteUInt(buf, len);
        buf[2] = a.code[0];
        buf[3] = a.code[1];
        fwrite(buf, 1, 4, m_seqFile);
        fwrite(&a.data[0], 1, a.data.size(), m_seqFile);
    }
    uint64_t pos = ftello(m_seqFile);
    if (pos != dataOffset) {
        char buf[4] = {0,0,0,0};
        fwrite(buf, 1, dataOffset - pos, m_seqFile);
    }
}

V1FSEQFile::V1FSEQFile(const std::string &fn, FILE *file, const std::vector<uint8_t> &header)
: FSEQFile(fn, file, header) {
    
    // m_seqNumUniverses = (header[20])       + (header[21] << 8);
    // m_seqUniverseSize = (header[22])       + (header[23] << 8);
    // m_seqGamma         = header[24];
    // m_seqColorEncoding = header[25];
    
    // 0 = header[26]
    // 0 = header[27]
    parseVariableHeaders(header, 28);
    
    //use the last modified time for the uniqueId
    struct stat stats;
    fstat(fileno(m_seqFile), &stats);
    m_uniqueId = stats.st_mtime;
}
V1FSEQFile::~V1FSEQFile() {
    
}
class UncompressedFrameData : public FrameData {
public:
    UncompressedFrameData(uint32_t frame,
                          uint32_t sz,
                          const std::vector<std::pair<uint32_t, uint32_t>> &ranges)
    : FrameData(frame), m_ranges(ranges) {
        m_data = (uint8_t*)malloc(sz);
    }
    virtual ~UncompressedFrameData() {
        free(m_data);
    }
    
    virtual void readFrame(uint8_t *data) {
        uint32_t offset = 0;
        for (auto &rng : m_ranges) {
            uint32_t toRead = rng.second;
            memcpy(&data[rng.first], &m_data[offset], toRead);
            offset += toRead;
        }
    }
    uint8_t *m_data;
    std::vector<std::pair<uint32_t, uint32_t>> m_ranges;
};
void V1FSEQFile::prepareRead(const std::vector<std::pair<uint32_t, uint32_t>> &ranges) {
    m_rangesToRead = ranges;
    m_dataBlockSize = 0;
    for (auto &rng : m_rangesToRead) {
        //make sure we don't read beyond the end of the sequence data
        int toRead = rng.second;
        if ((rng.first + toRead) > m_seqChannelCount) {
            toRead = m_seqChannelCount - rng.first;
            rng.second = toRead;
        }
        m_dataBlockSize += toRead;
    }
    FrameData *f = getFrame(0);
    if (f) {
        delete f;
    }
}

FrameData *V1FSEQFile::getFrame(uint32_t frame) {
    uint64_t offset = m_seqChannelCount;
    offset *= frame;
    offset += m_seqChanDataOffset;
    
    UncompressedFrameData *data = new UncompressedFrameData(frame, m_dataBlockSize, m_rangesToRead);
    if (fseeko(m_seqFile, offset, SEEK_SET)) {
        LogErr(VB_SEQUENCE, "Failed to seek to proper offset for channel data for frame %d! %lld\n", frame, offset);
        return data;
    }
    uint32_t sz = 0;
    //read the ranges into the buffer
    for (auto &rng : data->m_ranges) {
        if (rng.first < m_seqChannelCount) {
            int toRead = rng.second;
            uint64_t doffset = offset;
            doffset += rng.first;
            fseeko(m_seqFile, doffset, SEEK_SET);
            size_t bread = fread(&data->m_data[sz], 1, toRead, m_seqFile);
            if (bread != toRead) {
                LogErr(VB_SEQUENCE, "Failed to read channel data for frame %d!   Needed to read %d but read %d\n",
                       frame, toRead, (int)bread);
            }
            sz += toRead;
        }
    }
    return data;
}

void V1FSEQFile::addFrame(uint32_t frame,
                          uint8_t *data) {
    fwrite(data, 1, m_seqChannelCount, m_seqFile);
}
void V1FSEQFile::finalize() {
}


static const int V2FSEQ_HEADER_SIZE = 32;

V2FSEQFile::V2FSEQFile(const std::string &fn, CompressionType ct, int cl)
    : FSEQFile(fn),
    m_cctx(nullptr),
    m_dctx(nullptr),
    m_compressionType(ct),
    m_compressionLevel(cl)
{
    m_outBuffer.pos = 0;
    m_outBuffer.size = 1024*1024;
    m_outBuffer.dst = malloc(m_outBuffer.size);
    m_inBuffer.src = nullptr;
    m_inBuffer.size = 0;
    m_inBuffer.pos = 0;
}
void V2FSEQFile::writeHeader() {
    if (!m_sparseRanges.empty()) {
        //make sure the sparse ranges fit, and then
        //recalculate the channel count for in the fseq
        for (auto &a : m_sparseRanges) {
            if (a.first + a.second > m_seqChannelCount) {
                a.second = m_seqChannelCount - a.first;
            }
        }
        m_seqChannelCount = 0;
        for (auto &a : m_sparseRanges) {
            m_seqChannelCount += a.second;
        }
    }
    
    uint8_t header[V2FSEQ_HEADER_SIZE];
    memset(header, 0, V2FSEQ_HEADER_SIZE);
    header[0] = 'P';
    header[1] = 'S';
    header[2] = 'E';
    header[3] = 'Q';
    
    header[6] = 0; //minor
    header[7] = 2; //major
    
    // Step Size
    write4ByteUInt(&header[10], m_seqChannelCount);
    // Number of Steps
    write4ByteUInt(&header[14], m_seqNumFrames);
    // Step time in ms
    write2ByteUInt(&header[18], m_seqStepTime);

    // compression type
    header[20] = m_compressionType == CompressionType::none ? 0 : 1;
    //num blocks in compression index, (ignored if not compressed)
    header[21] = 0;
    //num ranges in sparse range index
    header[22] = m_sparseRanges.size();
    //reserved for future use
    header[23] = 0;

    
    //24-31 - timestamp/uuid/identifier
    if (m_uniqueId == 0) {
        m_uniqueId = GetTime();
    }
    memcpy(&header[24], &m_uniqueId, sizeof(m_uniqueId));
    
    if (m_compressionType != CompressionType::none) {
        //determine a good number of compression blocks
        uint64_t datasize = m_seqChannelCount * m_seqNumFrames;
        uint64_t blockSize = 131072; //at least 128K per block
        uint64_t numBlocks = datasize / blockSize;
        while (numBlocks > 255) {
            if (blockSize < 2028*1024) {
                blockSize *= 2;
            } else {
                blockSize += 1024*1024;
            }
            numBlocks = datasize / blockSize;
        }
        if (numBlocks < 1) numBlocks = 1;
        m_framesPerBlock = m_seqNumFrames / numBlocks;
        if (m_framesPerBlock < 10) m_framesPerBlock = 10;
        m_curFrameInBlock = 0;
        m_curBlock = 0;
        
        numBlocks = m_seqNumFrames / m_framesPerBlock + 1;
        m_maxBlocks = numBlocks;
    } else {
        m_maxBlocks = 0;
    }
    
    // index size
    header[21] = m_maxBlocks;

    int headerSize = V2FSEQ_HEADER_SIZE + m_maxBlocks * 8 + m_sparseRanges.size() * 6;

    // Fixed header length
    write2ByteUInt(&header[8], headerSize);
    
    int dataOffset = headerSize;
    for (auto &a : m_variableHeaders) {
        dataOffset += a.data.size() + 4;
    }
    dataOffset = roundTo4(dataOffset);
    write2ByteUInt(&header[4], dataOffset);
    
    fwrite(header, 1, V2FSEQ_HEADER_SIZE, m_seqFile);
    for (int x = 0; x < m_maxBlocks; x++) {
        uint8_t buf[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        //frame number and len
        fwrite(buf, 1, 8, m_seqFile);
    }
    for (auto &a : m_sparseRanges) {
        uint8_t buf[6] = {0, 0, 0, 0, 0, 0};
        write3ByteUInt(buf, a.first);
        write3ByteUInt(&buf[3], a.second);
        fwrite(buf, 1, 6, m_seqFile);
    }
    for (auto &a : m_variableHeaders) {
        uint8_t buf[4];
        uint32_t len = a.data.size() + 4;
        write2ByteUInt(buf, len);
        buf[2] = a.code[0];
        buf[3] = a.code[1];
        fwrite(buf, 1, 4, m_seqFile);
        fwrite(&a.data[0], 1, a.data.size(), m_seqFile);
    }
    uint64_t pos = ftello(m_seqFile);
    if (pos != dataOffset) {
        char buf[4] = {0,0,0,0};
        fwrite(buf, 1, dataOffset - pos, m_seqFile);
    }
}


V2FSEQFile::V2FSEQFile(const std::string &fn, FILE *file, const std::vector<uint8_t> &header)
: FSEQFile(fn, file, header), m_compressionType(none),
m_cctx(nullptr),
m_dctx(nullptr)
{
    m_outBuffer.pos = 0;
    m_outBuffer.size = 1024*1024;
    m_outBuffer.dst = malloc(m_outBuffer.size);
    m_inBuffer.src = nullptr;
    m_inBuffer.size = 0;
    m_inBuffer.pos = 0;
    
    //24-31 - timestamp/uuid/identifier
    uint64_t *a = (uint64_t*)&header[24];
    m_uniqueId = *a;
    
    m_maxBlocks = header[21];
    switch (header[20]) {
        case 0:
            m_compressionType = CompressionType::none;
            break;
        case 1:
            m_compressionType = CompressionType::zstd;
            break;
        //case 2:
            //m_compressionType = CompressionType::zlib;
            //break;
        default:
            LogErr(VB_SEQUENCE, "Unknown compression type: %d", (int)header[32]);
    }
    
    m_curBlock = 9999;

    uint64_t offset = m_seqChanDataOffset;
    int hoffset = V2FSEQ_HEADER_SIZE;
    for (int x = 0; x < m_maxBlocks; x++) {
        int frame = read4ByteUInt(&header[hoffset]);
        hoffset += 4;
        uint64_t dlen = read4ByteUInt(&header[hoffset]);
        hoffset += 4;
        if (dlen > 0) {
            m_frameOffsets.push_back(std::pair<uint32_t, uint64_t>(frame, offset));
            offset += dlen;
        }
        if (x == 0) {
            uint64_t doff = m_seqChanDataOffset;
            posix_fadvise(fileno(m_seqFile), doff, dlen, POSIX_FADV_WILLNEED);
        }
    }
    
    m_frameOffsets.push_back(std::pair<uint32_t, uint64_t>(99999999, offset));
    //sparse ranges
    for (int x = 0; x < header[22]; x++) {
        uint32_t st = read3ByteUInt(&header[hoffset]);
        uint32_t len = read3ByteUInt(&header[hoffset + 3]);
        hoffset += 6;
        m_sparseRanges.push_back(std::pair<uint32_t, uint32_t>(st, len));
    }
    
    parseVariableHeaders(header, hoffset);
}
V2FSEQFile::~V2FSEQFile() {
    free(m_outBuffer.dst);
    if (m_inBuffer.src != nullptr) {
        free((void*)m_inBuffer.src);
    }
    if (m_dctx) {
        ZSTD_freeCStream(m_cctx);
    }
    if (m_dctx) {
        ZSTD_freeDStream(m_dctx);
    }
}

void V2FSEQFile::prepareRead(const std::vector<std::pair<uint32_t, uint32_t>> &ranges) {
    if (m_sparseRanges.empty()) {
        m_rangesToRead = ranges;
        m_dataBlockSize = 0;
        for (auto &rng : m_rangesToRead) {
            //make sure we don't read beyond the end of the sequence data
            int toRead = rng.second;
            if ((rng.first + toRead) > m_seqChannelCount) {
                toRead = m_seqChannelCount - rng.first;
                rng.second = toRead;
            }
            m_dataBlockSize += toRead;
        }
    } else if (m_compressionType != CompressionType::none) {
        //with compression, there is no way to NOT read the entire frame of data, we'll just
        //use the sparse data range since we'll have everything anyway so the ranges
        //needed is relatively irrelevant
        m_dataBlockSize = m_seqChannelCount;
        m_rangesToRead = m_sparseRanges;
    } else {
        //no compression with sparse ranges
        //FIXME - an intersection between the two would be useful, but hard
        //for now, just assume that if it's sparse, it has all the data that is needed
        //and read everything
        m_dataBlockSize = m_seqChannelCount;
        m_rangesToRead = m_sparseRanges;
    }
    FrameData *f = getFrame(0);
    if (f) {
        delete f;
    }
}
FrameData *V2FSEQFile::getFrame(uint32_t frame) {
    if (frame >= m_seqNumFrames) {
        return nullptr;
    }
    if (m_compressionType == CompressionType::none) {
        return getFrameNone(frame);
    }
    return getFrameZSTD(frame);
}
FrameData *V2FSEQFile::getFrameNone(uint32_t frame) {
    UncompressedFrameData *data = new UncompressedFrameData(frame, m_dataBlockSize, m_rangesToRead);
    uint64_t offset = m_seqChannelCount;
    offset *= frame;
    offset += m_seqChanDataOffset;
    if (fseeko(m_seqFile, offset, SEEK_SET)) {
        LogErr(VB_SEQUENCE, "Failed to seek to proper offset for channel data! %lld\n", offset);
        return data;
    }
    if (m_sparseRanges.empty()) {
        uint32_t sz = 0;
        //read the ranges into the buffer
        for (auto &rng : data->m_ranges) {
            if (rng.first < m_seqChannelCount) {
                int toRead = rng.second;
                uint64_t doffset = offset;
                doffset += rng.first;
                fseeko(m_seqFile, doffset, SEEK_SET);
                size_t bread = fread(&data->m_data[sz], 1, toRead, m_seqFile);
                if (bread != toRead) {
                    LogErr(VB_SEQUENCE, "Failed to read channel data!   Needed to read %d but read %d\n", toRead, (int)bread);
                }
                sz += toRead;
            }
        }
    } else {
        size_t bread = fread(data->m_data, 1, m_dataBlockSize, m_seqFile);
        if (bread != m_dataBlockSize) {
            LogErr(VB_SEQUENCE, "Failed to read channel data!   Needed to read %d but read %d\n", m_dataBlockSize, (int)bread);
        }
    }
    return data;
}
FrameData *V2FSEQFile::getFrameZSTD(uint32_t frame) {
    if (m_curBlock > 256 || (frame < m_frameOffsets[m_curBlock].first) || (frame >= m_frameOffsets[m_curBlock + 1].first)) {
        //frame is not in the current block
        m_curBlock = 0;
        while (frame >= m_frameOffsets[m_curBlock + 1].first) {
            m_curBlock++;
        }
        if (m_dctx == nullptr) {
            m_dctx = ZSTD_createDStream();
        }
        ZSTD_initDStream(m_dctx);
        fseeko(m_seqFile, m_frameOffsets[m_curBlock].second, SEEK_SET);
        
        uint64_t len = m_frameOffsets[m_curBlock + 1].second;
        len -= m_frameOffsets[m_curBlock].second;
        if (m_inBuffer.src) {
            free((void*)m_inBuffer.src);
        }
        m_inBuffer.src = malloc(len);
        m_inBuffer.pos = 0;
        m_inBuffer.size = len;
        int bread = fread((void*)m_inBuffer.src, 1, len, m_seqFile);
        if (bread != len) {
            LogErr(VB_SEQUENCE, "Failed to read channel data!   Needed to read %d but read %d\n", len, (int)bread);
        }
        
        if (m_curBlock < m_frameOffsets.size() - 2) {
            //let the kernel know that we'll likely need the next block in the near future
            uint64_t len = m_frameOffsets[m_curBlock + 2].second;
            len -= m_frameOffsets[m_curBlock+1].second;
            posix_fadvise(fileno(m_seqFile), ftello(m_seqFile), len, POSIX_FADV_WILLNEED);
        }

        free(m_outBuffer.dst);
        int numFrames = (m_frameOffsets[m_curBlock + 1].first > m_seqNumFrames ? m_seqNumFrames :  m_frameOffsets[m_curBlock + 1].first) - m_frameOffsets[m_curBlock].first;
        m_outBuffer.size = numFrames * m_seqChannelCount;
        m_outBuffer.dst = malloc(m_outBuffer.size);
        m_outBuffer.pos = 0;
        ZSTD_decompressStream(m_dctx, &m_outBuffer, &m_inBuffer);
    }
    int fidx = frame - m_frameOffsets[m_curBlock].first;
    fidx *= m_seqChannelCount;
    uint8_t *fdata = (uint8_t*)m_outBuffer.dst;
    UncompressedFrameData *data = new UncompressedFrameData(frame, m_dataBlockSize, m_rangesToRead);
    if (!m_sparseRanges.empty()) {
        memcpy(data->m_data, &fdata[fidx], m_seqChannelCount);
    } else {
        uint32_t sz = 0;
        //read the ranges into the buffer
        for (auto &rng : data->m_ranges) {
            if (rng.first < m_seqChannelCount) {
                memcpy(&data->m_data[sz], &fdata[fidx + rng.first], rng.second);
                sz += rng.second;
            }
        }
    }
    return data;
}
void V2FSEQFile::addFrame(uint32_t frame,
                          uint8_t *data) {
    if (m_compressionType == CompressionType::none) {
        addFrameNone(frame, data);
    } else {
        addFrameZSTD(frame, data);
    }
}
void V2FSEQFile::addFrameNone(uint32_t frame, uint8_t *data) {
    if (m_sparseRanges.empty()) {
        fwrite(data, 1, m_seqChannelCount, m_seqFile);
    } else {
        for (auto &a : m_sparseRanges) {
            fwrite(&data[a.first], 1, a.second, m_seqFile);
        }
    }
}

inline void compressData(FILE* m_seqFile, ZSTD_CStream* m_cctx, ZSTD_inBuffer_s &input, ZSTD_outBuffer_s &output) {
    ZSTD_compressStream(m_cctx, &output, &input);
    int count = input.pos;
    int total = input.size;
    uint8_t *curData = (uint8_t*)input.src;
    while (count < total) {
        count += input.pos;
        curData += input.pos;
        input.src = curData;
        input.size -= input.pos;
        input.pos = 0;
        if (output.pos) {
            fwrite(output.dst, 1, output.pos, m_seqFile);
            output.pos = 0;
        }
        ZSTD_compressStream(m_cctx, &output, &input);
        count += input.pos;
    }
}
void V2FSEQFile::addFrameZSTD(uint32_t frame, uint8_t *data) {

    if (m_cctx == nullptr) {
        m_cctx = ZSTD_createCStream();
    }
    if (m_curFrameInBlock == 0) {
        uint64_t offset = ftello(m_seqFile);
        m_frameOffsets.push_back(std::pair<uint32_t, uint64_t>(frame, offset));
        ZSTD_initCStream(m_cctx, m_compressionLevel);
    }
    
    uint8_t *curData = data;
    
    if (m_sparseRanges.empty()) {
        ZSTD_inBuffer_s input = {
            curData,
            m_seqChannelCount,
            0
        };
        compressData(m_seqFile, m_cctx, input, m_outBuffer);
    } else {
        for (auto &a : m_sparseRanges) {
            ZSTD_inBuffer_s input = {
                &curData[a.first],
                a.second,
                0
            };
            compressData(m_seqFile, m_cctx, input, m_outBuffer);
        }
    }

    if (m_outBuffer.pos) {
        fwrite(m_outBuffer.dst, 1, m_outBuffer.pos, m_seqFile);
        m_outBuffer.pos = 0;
    }
    
    m_curFrameInBlock++;
    if (m_curFrameInBlock == m_framesPerBlock && m_frameOffsets.size() < m_maxBlocks) {
        while (ZSTD_flushStream(m_cctx, &m_outBuffer) > 0) {
            fwrite(m_outBuffer.dst, 1, m_outBuffer.pos, m_seqFile);
            m_outBuffer.pos = 0;
        }
        if (m_outBuffer.pos) {
            fwrite(m_outBuffer.dst, 1, m_outBuffer.pos, m_seqFile);
            m_outBuffer.pos = 0;
        }
        ZSTD_endStream(m_cctx, &m_outBuffer);
        fwrite(m_outBuffer.dst, 1, m_outBuffer.pos, m_seqFile);
        m_outBuffer.pos = 0;
        m_curFrameInBlock = 0;
        m_curBlock = 0;
    }
}
void V2FSEQFile::finalize() {
    if (m_compressionType == CompressionType::none) {
        //don't need to do anything
    } else {
        if (m_curFrameInBlock) {
            while (ZSTD_flushStream(m_cctx, &m_outBuffer) > 0) {
                fwrite(m_outBuffer.dst, 1, m_outBuffer.pos, m_seqFile);
                m_outBuffer.pos = 0;
            }
            if (m_outBuffer.pos) {
                fwrite(m_outBuffer.dst, 1, m_outBuffer.pos, m_seqFile);
                m_outBuffer.pos = 0;
            }
            ZSTD_endStream(m_cctx, &m_outBuffer);
            fwrite(m_outBuffer.dst, 1, m_outBuffer.pos, m_seqFile);
            m_outBuffer.pos = 0;
            m_curFrameInBlock = 0;
            m_curBlock = 0;
        }
        
        uint64_t curr = ftello(m_seqFile);
        uint64_t off = V2FSEQ_HEADER_SIZE;
        fseek(m_seqFile, off, SEEK_SET);
        int count = m_frameOffsets.size();
        m_frameOffsets.push_back(std::pair<uint32_t, uint64_t>(99999999, curr));
        for (int x = 0 ; x < count; x++) {
            uint8_t buf[8];
            uint32_t frame = m_frameOffsets[x].first;
            write4ByteUInt(buf,frame);
            
            uint64_t len64 = m_frameOffsets[x + 1].second;
            len64 -= m_frameOffsets[x].second;
            uint32_t len = len64;
            write4ByteUInt(&buf[4], len);
            fwrite(buf, 1, 8, m_seqFile);
        }
        m_frameOffsets.pop_back();
    }
}
