#include <vector>
#include <cstring>

#include <zstd.h>

#include <stdio.h>
#include <inttypes.h>

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
    int seqChanDataOffset = tmpData[4] + (tmpData[5] << 8);
    std::vector<uint8_t> header(seqChanDataOffset);
    fseeko(seqFile, 0L, SEEK_SET);
    bytesRead = fread(&header[0], 1, seqChanDataOffset, seqFile);
    if (bytesRead != seqChanDataOffset) {
        LogErr(VB_SEQUENCE, "Error opening sequence file: %s. Could not read header.\n", fn.c_str());
        HexDump("Sequence File head:", &header[0], bytesRead);
        fclose(seqFile);
    }
    
    if (seqVersionMajor == 1) {
        return new V1FSEQFile(fn, seqFile, header);
    } else if (seqVersionMajor == 2) {
        return new V2FSEQFile(fn, seqFile, header);
    }
    LogErr(VB_SEQUENCE, "Error opening sequence file: %s. Unknown FSEQ version %d-%d\n",
           fn.c_str(), seqVersionMajor, seqVersionMinor);
    HexDump("Sequence File head:", tmpData, bytesRead);
    fclose(seqFile);
    return nullptr;
}

FSEQFile::FSEQFile(const std::string &fn,
                   uint32_t     seqNumFrames,
                   uint32_t     seqStartChannel,
                   uint32_t     seqChannelCount,
                   int          seqStepTime,
                   const std::vector<VariableHeader> &headers)
    : filename(fn),
    m_seqNumFrames(seqNumFrames),
    m_seqStartChannel(seqStartChannel),
    m_seqChannelCount(seqChannelCount),
    m_seqStepTime(seqStepTime),
    m_variableHeaders(headers)
{
    m_seqFile = fopen((const char *)fn.c_str(), "w");
}

FSEQFile* FSEQFile::createFSEQFile(const std::string &fn,
                                   int version,
                                   uint32_t     seqNumFrames,
                                   uint32_t     seqStartChannel,
                                   uint32_t     seqChannelCount,
                                   int          seqStepTime,
                                   const std::vector<VariableHeader> &headers) {
    if (version == 1) {
        return new V1FSEQFile(fn, seqNumFrames, seqStartChannel, seqChannelCount, seqStepTime, headers);
    }
    return new V2FSEQFile(fn, seqNumFrames, seqStartChannel, seqChannelCount, seqStepTime, headers);
}




FSEQFile::FSEQFile(const std::string &fn, FILE *file, const std::vector<uint8_t> &header)
    : filename(fn), m_seqFile(file), m_seqStartChannel(0) {
    fseeko(m_seqFile, 0L, SEEK_END);
    m_seqFileSize = ftello(m_seqFile);
    fseeko(m_seqFile, 0L, SEEK_SET);

    m_seqChanDataOffset = header[4] + (header[5] << 8);
    m_seqVersionMinor = header[6];
    m_seqVersionMajor = header[7];
    m_seqVersion      = (m_seqVersionMajor * 256) + m_seqVersionMinor;

    //m_seqFixedHeaderSize = (header[8]) + (header[9] << 8);
    
    m_seqChannelCount = (header[10])       + (header[11] << 8) +
                    (header[12] << 16) + (header[13] << 24);
    
    m_seqNumFrames = (header[14])       + (header[15] << 8) +
                      (header[16] << 16) + (header[17] << 24);
    
    m_seqStepTime = (header[18])       + (header[19] << 8);
    
    
}
FSEQFile::~FSEQFile() {
    fclose(m_seqFile);
}
void FSEQFile::parseVariableHeaders(const std::vector<uint8_t> &header, int start) {
    while (start < header.size() - 5) {
        int len = header[start] + (header[start + 1] << 8);
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


V1FSEQFile::V1FSEQFile(const std::string &fn,
                       uint32_t     seqNumFrames,
                       uint32_t     seqStartChannel,
                       uint32_t     seqChannelCount,
                       int          seqStepTime,
                       const std::vector<VariableHeader> &headers)
  : FSEQFile(fn, seqNumFrames, seqStartChannel, seqChannelCount, seqStepTime, headers)
{
    static int fixedHeaderLength = 28;
    uint8_t header[28];
    memset(header, 0, 28);
    header[0] = 'P';
    header[1] = 'S';
    header[2] = 'E';
    header[3] = 'Q';

    // data offset
    int dataOffset = fixedHeaderLength;
    for (auto &a : m_variableHeaders) {
        dataOffset += a.data.size() + 4;
    }
    dataOffset = roundTo4(dataOffset);
    
    header[4] = (uint8_t)(dataOffset % 256);
    header[5] = (uint8_t)(dataOffset / 256);

    header[6] = 0; //minor
    header[7] = 1; //major
    // Fixed header length
    header[8] = (uint8_t)(fixedHeaderLength % 256);
    header[9] = (uint8_t)(fixedHeaderLength / 256);
    // Step Size
    header[10] = (uint8_t)(seqChannelCount & 0xFF);
    header[11] = (uint8_t)((seqChannelCount >> 8) & 0xFF);
    header[12] = (uint8_t)((seqChannelCount >> 16) & 0xFF);
    header[13] = (uint8_t)((seqChannelCount >> 24) & 0xFF);
    // Number of Steps
    header[14] = (uint8_t)(seqNumFrames & 0xFF);
    header[15] = (uint8_t)((seqNumFrames >> 8) & 0xFF);
    header[16] = (uint8_t)((seqNumFrames >> 16) & 0xFF);
    header[17] = (uint8_t)((seqNumFrames >> 24) & 0xFF);
    // Step time in ms
    header[18] = (uint8_t)(seqStepTime & 0xFF);
    header[19] = (uint8_t)((seqStepTime >> 8) & 0xFF);
    // universe count
    header[20] = 0;
    header[21] = 0;
    // universe Size
    header[22] = 0;
    header[23] = 0;
    // universe Size
    header[24] = 1;
    // color order
    header[25] = 2;
    header[26] = 0;
    header[27] = 0;
    fwrite(header, 1, 28, m_seqFile);
    for (auto &a : m_variableHeaders) {
        char buf[4];
        int len = a.data.size() + 4;
        buf[0] = (uint8_t)(len & 0xFF);
        buf[1] = (uint8_t)((len >> 8) & 0xFF);
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
}
V1FSEQFile::~V1FSEQFile() {
    
}

void V1FSEQFile::readFrame(uint32_t frame,
                           uint8_t *data,
                           const std::vector<std::pair<uint32_t, uint32_t>> ranges) {
    uint64_t offset = m_seqChannelCount;
    offset *= frame;
    offset += m_seqChanDataOffset;
    if (fseeko(m_seqFile, offset, SEEK_SET)) {
        LogErr(VB_SEQUENCE, "Failed to seek to proper offset for channel data! %lld\n", offset);
        for (auto &rng : ranges) {
            memset(&data[rng.first], 0, rng.second);
        }
    }
    for (auto &rng : ranges) {
        if (rng.first < m_seqChannelCount) {
            int toRead = rng.second;
            if ((rng.first + toRead) > m_seqChannelCount) {
                toRead = m_seqChannelCount - rng.first;
            }
            uint64_t doffset = offset;
            doffset += rng.first;
            fseeko(m_seqFile, doffset, SEEK_SET);
            size_t bread = fread(&data[rng.first], 1, toRead, m_seqFile);
            if (bread != toRead) {
                LogErr(VB_SEQUENCE, "Failed to read channel data!   Needed to read %d but read %d\n", toRead, (int)bread);
            }
        }
    }
}

void V1FSEQFile::addFrame(uint32_t frame,
                          uint8_t *data) {
    fwrite(data, 1, m_seqChannelCount, m_seqFile);
}
void V1FSEQFile::finalize() {
}


static const int V2FSEQ_HEADER_SIZE = 34;

V2FSEQFile::V2FSEQFile(const std::string &fn,
                       uint32_t     seqNumFrames,
                       uint32_t     seqStartChannel,
                       uint32_t     seqChannelCount,
                       int          seqStepTime,
                       const std::vector<VariableHeader> &headers)
    : FSEQFile(fn, seqNumFrames, seqStartChannel, seqChannelCount, seqStepTime, headers),
m_cctx(nullptr),
m_dctx(nullptr)
{
    m_outBuffer.pos = 0;
    m_outBuffer.size = 1024*1024;
    m_outBuffer.dst = malloc(m_outBuffer.size);
    m_inBuffer.src = nullptr;
    m_inBuffer.size = 0;
    m_inBuffer.pos = 0;

    uint8_t header[V2FSEQ_HEADER_SIZE];
    memset(header, 0, V2FSEQ_HEADER_SIZE);
    header[0] = 'P';
    header[1] = 'S';
    header[2] = 'E';
    header[3] = 'Q';
    
    header[6] = 0; //minor
    header[7] = 2; //major
    
    // Step Size
    header[10] = (uint8_t)(seqChannelCount & 0xFF);
    header[11] = (uint8_t)((seqChannelCount >> 8) & 0xFF);
    header[12] = (uint8_t)((seqChannelCount >> 16) & 0xFF);
    header[13] = (uint8_t)((seqChannelCount >> 24) & 0xFF);
    // Number of Steps
    header[14] = (uint8_t)(seqNumFrames & 0xFF);
    header[15] = (uint8_t)((seqNumFrames >> 8) & 0xFF);
    header[16] = (uint8_t)((seqNumFrames >> 16) & 0xFF);
    header[17] = (uint8_t)((seqNumFrames >> 24) & 0xFF);
    // Step time in ms
    header[18] = (uint8_t)(seqStepTime & 0xFF);
    header[19] = (uint8_t)((seqStepTime >> 8) & 0xFF);
    // universe count
    header[20] = (uint8_t)(seqStartChannel & 0xFF);
    header[21] = (uint8_t)((seqStartChannel >> 8) & 0xFF);
    header[22] = (uint8_t)((seqStartChannel >> 16) & 0xFF);
    header[23] = (uint8_t)((seqStartChannel >> 24) & 0xFF);
    
    //24-31 - timestamp/uuid/identifier
    long long ts = GetTime();
    memcpy(&header[24], &ts, sizeof(ts));
    
    // zstd compression
    header[32] = 1;

    //determine a good number of compression blocks
    uint64_t datasize = seqChannelCount * seqNumFrames;
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
    m_framesPerBlock = seqNumFrames / numBlocks;
    if (m_framesPerBlock < 10) m_framesPerBlock = 10;
    m_curFrameInBlock = 0;
    m_curBlock = 0;
    
    numBlocks = seqNumFrames / m_framesPerBlock + 1;
    
    m_maxBlocks = numBlocks;
    
    // index size
    header[33] = numBlocks;

    int headerSize = V2FSEQ_HEADER_SIZE + numBlocks * 8;
    
    // Fixed header length
    header[8] = (uint8_t)(headerSize % 256);
    header[9] = (uint8_t)(headerSize / 256);
    
    int dataOffset = headerSize;
    for (auto &a : m_variableHeaders) {
        dataOffset += a.data.size() + 4;
    }
    dataOffset = roundTo4(dataOffset);
    header[4] = (uint8_t)(dataOffset % 256);
    header[5] = (uint8_t)(dataOffset / 256);

    fwrite(header, 1, V2FSEQ_HEADER_SIZE, m_seqFile);
    for (int x = 0; x < numBlocks; x++) {
        uint8_t buf[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        //frame number and len
        fwrite(buf, 1, 8, m_seqFile);
    }
    for (auto &a : m_variableHeaders) {
        char buf[4];
        int len = a.data.size() + 4;
        buf[0] = (uint8_t)(len & 0xFF);
        buf[1] = (uint8_t)((len >> 8) & 0xFF);
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

    m_seqStartChannel = (header[20])  + (header[21] << 8) +
        (header[22] << 16) + (header[23] << 24);
    
    //24-31 - timestamp/uuid/identifier
    uint64_t *a = (uint64_t*)&header[24];
    m_uniqueId = *a;
    
    switch (header[32]) {
        //case 0:
            //m_compressionType = CompressionType::none;
            //break;
        case 1:
            m_compressionType = CompressionType::zstd;
            break;
        //case 2:
            //m_compressionType = CompressionType::zlib;
            //break;
        default:
            LogErr(VB_SEQUENCE, "Unknown compression type: %d", (int)header[32]);
    }
    
    int numCompBlocks = header[33];
    m_curBlock = 9999;

    uint64_t offset = m_seqChanDataOffset;
    for (int x = 0; x < numCompBlocks; x++) {
        int hoffset = 34 + x * 8;
        int frame = (header[hoffset])       + (header[hoffset + 1] << 8) +
            (header[hoffset + 2] << 16) + (header[hoffset + 3] << 24);
        hoffset += 4;
        uint64_t dlen = (header[hoffset])       + (header[hoffset + 1] << 8) +
            (header[hoffset + 2] << 16) + (header[hoffset + 3] << 24);
        
        if (dlen > 0) {
            m_frameOffsets.push_back(std::pair<uint32_t, uint64_t>(frame, offset));
            offset += dlen;
        }
    }
    m_frameOffsets.push_back(std::pair<uint32_t, uint64_t>(99999999, offset));
    
    parseVariableHeaders(header, V2FSEQ_HEADER_SIZE + numCompBlocks*8);
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
void V2FSEQFile::readFrame(uint32_t frame,
                           uint8_t *data,
                           const std::vector<std::pair<uint32_t, uint32_t>> ranges) {
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
    memcpy(&data[m_seqStartChannel], &fdata[fidx], m_seqChannelCount);
}
void V2FSEQFile::addFrame(uint32_t frame,
                          uint8_t *data) {
    if (m_cctx == nullptr) {
        m_cctx = ZSTD_createCStream();
    }
    if (m_curFrameInBlock == 0) {
        uint64_t offset = ftello(m_seqFile);
        m_frameOffsets.push_back(std::pair<uint32_t, uint64_t>(frame, offset));
        ZSTD_initCStream(m_cctx, 10);
    }
    
    uint8_t *curData = &data[m_seqStartChannel];
    ZSTD_inBuffer_s input = {
        curData,
        m_seqChannelCount,
        0
    };
    ZSTD_compressStream(m_cctx, &m_outBuffer, &input);
    int count = input.pos;
    while (count < m_seqChannelCount) {
        count += input.pos;
        curData += input.pos;
        input.src = curData;
        input.size -= input.pos;
        input.pos = 0;
        if (m_outBuffer.pos) {
            fwrite(m_outBuffer.dst, 1, m_outBuffer.pos, m_seqFile);
            m_outBuffer.pos = 0;
        }
        ZSTD_compressStream(m_cctx, &m_outBuffer, &input);
        count += input.pos;
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
    uint64_t off = 34;
    fseek(m_seqFile, off, SEEK_SET);
    int count = m_frameOffsets.size();
    m_frameOffsets.push_back(std::pair<uint32_t, uint64_t>(99999999, curr));
    for (int x = 0 ; x < count; x++) {
        uint8_t buf[8];
        int frame = m_frameOffsets[x].first;
        buf[0] = (uint8_t)(frame & 0xFF);
        buf[1] = (uint8_t)((frame >> 8) & 0xFF);
        buf[2] = (uint8_t)((frame >> 16) & 0xFF);
        buf[3] = (uint8_t)((frame >> 24) & 0xFF);
        
        uint64_t len64 = m_frameOffsets[x + 1].second;
        len64 -= m_frameOffsets[x].second;
        uint32_t len = len64;
        buf[4] = (uint8_t)(len & 0xFF);
        buf[5] = (uint8_t)((len >> 8) & 0xFF);
        buf[6] = (uint8_t)((len >> 16) & 0xFF);
        buf[7] = (uint8_t)((len >> 24) & 0xFF);
        fwrite(buf, 1, 8, m_seqFile);
    }
    m_frameOffsets.pop_back();
}
