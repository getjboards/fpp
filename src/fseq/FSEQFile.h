#ifndef __FSEQFILE_H_

#include <stdio.h>
#include <string>
#include <vector>

#include <zstd.h>

class VariableHeader {
public:
    VariableHeader() { code[0] = code[1] = 0; }
    VariableHeader(const VariableHeader& cp) : data(cp.data) {
        code[0] = cp.code[0];
        code[1] = cp.code[1];
    }
    ~VariableHeader() {}
    
    uint8_t code[2];
    std::vector<uint8_t> data;
};


class FSEQFile {
public:
    
protected:
    //open file for reading
    FSEQFile(const std::string &fn, FILE *file, const std::vector<uint8_t> &header);
    //open file for writing
    FSEQFile(const std::string &fn,
             uint32_t     seqNumFrames,
             uint32_t     seqStartChannel,
             uint32_t     seqChannelCount,
             int          seqStepTime,
             const std::vector<VariableHeader> &headers);
    
public:
    
    virtual ~FSEQFile();
    
    static FSEQFile* openFSEQFile(const std::string &fn);
    
    static FSEQFile* createFSEQFile(const std::string &fn,
                                    int version,
                                    uint32_t     seqNumFrames,
                                    uint32_t     seqStartChannel,
                                    uint32_t     seqChannelCount,
                                    int          seqStepTime,
                                    const std::vector<VariableHeader> &headers);
    
    void parseVariableHeaders(const std::vector<uint8_t> &header, int start);
    
    //For reading data from the fseq file
    virtual void readFrame(uint32_t frame,
                           uint8_t *data,
                           const std::vector<std::pair<uint32_t, uint32_t>> ranges) = 0;
    
    //For writing to the fseq file
    virtual void addFrame(uint32_t frame,
                          uint8_t *data) = 0;
    virtual void finalize() = 0;
    
    
    
    std::string filename;
    
    FILE* volatile  m_seqFile;
    
    uint64_t      m_seqFileSize;
    uint64_t      m_seqChanDataOffset;
    uint64_t      m_uniqueId;

    uint32_t      m_seqNumFrames;
    uint32_t      m_seqStartChannel;
    uint32_t      m_seqChannelCount;
    int           m_seqStepTime;
    int           m_seqVersionMajor;
    int           m_seqVersionMinor;
    int           m_seqVersion;
    
    std::vector<VariableHeader> m_variableHeaders;
};


class V1FSEQFile : public FSEQFile {
public:
    V1FSEQFile(const std::string &fn, FILE *file, const std::vector<uint8_t> &header);
    V1FSEQFile(const std::string &fn,
             uint32_t     seqNumFrames,
             uint32_t     seqStartChannel,
             uint32_t     seqChannelCount,
               int          seqStepTime,
               const std::vector<VariableHeader> &headers);

    virtual ~V1FSEQFile();
  
    virtual void readFrame(uint32_t frame,
                           uint8_t *data,
                           const std::vector<std::pair<uint32_t, uint32_t>> ranges);
    
    virtual void addFrame(uint32_t frame,
                          uint8_t *data);
    virtual void finalize();


};

class V2FSEQFile : public FSEQFile {

public:
    V2FSEQFile(const std::string &fn, FILE *file, const std::vector<uint8_t> &header);
    V2FSEQFile(const std::string &fn,
               uint32_t     seqNumFrames,
               uint32_t     seqStartChannel,
               uint32_t     seqChannelCount,
               int          seqStepTime,
               const std::vector<VariableHeader> &headers);

    virtual ~V2FSEQFile();
    
    virtual void readFrame(uint32_t frame,
                           uint8_t *data,
                           const std::vector<std::pair<uint32_t, uint32_t>> ranges);

    
    virtual void addFrame(uint32_t frame,
                          uint8_t *data);
    virtual void finalize();

    
    enum CompressionType {
        none,
        zstd,
        zlib
    };
    CompressionType m_compressionType;
    
    std::vector<std::pair<uint32_t, uint64_t>> m_frameOffsets;
    
    
    uint32_t m_framesPerBlock;
    uint32_t m_curFrameInBlock;
    uint32_t m_curBlock;
    uint32_t m_maxBlocks;
    ZSTD_CStream* m_cctx;
    ZSTD_DStream* m_dctx;
    ZSTD_outBuffer_s m_outBuffer;
    ZSTD_inBuffer_s m_inBuffer;
};


#endif
