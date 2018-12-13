
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "fppversion.h"
#include "common.h"

#include "FSEQFile.h"

void usage(char *appname) {
    printf("Usage: %s [OPTIONS] FileName.fseq\n", appname);
    printf("\n");
    printf("  Options:\n");
    printf("   -V                     - Print version information\n");
    printf("   -o OUTPUTFILE          - Filename for Output FSEQ\n");
    printf("   -f #                   - FSEQ Version\n");
    printf("   -c (none|zstd)         - Compession type\n");
    printf("   -l #                   - Compession level\n");
    printf("   -h                     - This help output\n");
}
const char *outputFilename = nullptr;
static int fseqVersion = 2;
static int compressionLevel = 10;
static V2FSEQFile::CompressionType compressionType = V2FSEQFile::CompressionType::zstd;

int parseArguments(int argc, char **argv) {
    char *s = NULL;
    int   c;
    
    int this_option_optind = optind;
    while (1) {
        this_option_optind = optind ? optind : 1;
        int option_index = 0;
        static struct option long_options[] = {
            {"help",           no_argument,          0, 'h'},
            {"output",         required_argument,    0, 'o'},
            {0,                0,                    0, 0}
        };
        
        c = getopt_long(argc, argv, "c:l:o:f:hV", long_options, &option_index);
        if (c == -1) {
            break;
        }
        
        switch (c) {
            case 'c':
                compressionType = strcmp(optarg, "none")
                    ? V2FSEQFile::CompressionType::zstd : V2FSEQFile::CompressionType::none;
                break;
            case 'l':
                compressionLevel = strtol(optarg, NULL, 10);
                break;
            case 'f':
                fseqVersion = strtol(optarg, NULL, 10);
                break;
            case 'o':
                outputFilename = optarg;
                break;
            case 'V':
                printVersionInfo();
                exit(0);
            case 'h':
                usage(argv[0]);
                exit(EXIT_SUCCESS);
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    return this_option_optind;
}

int main(int argc, char *argv[]) {
    int idx = parseArguments(argc, argv);
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    ranges.push_back(std::pair<uint32_t, uint32_t>(0, 999999999));
    
    FSEQFile *src = FSEQFile::openFSEQFile(argv[idx]);
    if (src) {
        src->prepareRead(ranges);
        
        FSEQFile *dest = FSEQFile::createFSEQFile(outputFilename,
                                                  fseqVersion,
                                                  compressionType,
                                                  compressionLevel);
        dest->initializeFromFSEQ(*src);
        dest->writeHeader();
        
        uint8_t data[1024*1024];
        for (int x = 0; x < src->m_seqNumFrames; x++) {
            FrameData *fdata = src->getFrame(x);
            fdata->readFrame(data);
            delete fdata;
            dest->addFrame(x, data);
        }
        dest->finalize();
        delete dest;
        delete src;
    }

    
    return 0;
}
