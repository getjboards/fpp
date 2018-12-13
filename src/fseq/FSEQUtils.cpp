
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "fppversion.h"
#include "common.h"

#include "FSEQFile.h"

void usage(char *appname) {
    printf("Usage: %s [OPTIONS] FileName.fseq\n", appname);
    printf("\n");
    printf("  Options:\n");
    printf("   -V                     - Print version information\n");
    printf("   -s #                   - Start Channel\n");
    printf("   -c #                   - Channel Count\n");
    printf("   -o OUTPUTFILE          - Filename for Output FSEQ\n");
    printf("   -f #                   - FSEQ Version\n");
    printf("   -h                     - This help output\n");
}
static int startChannel = 0;
static int channelCount = 999999999;
const char *outputFilename = nullptr;
static int fseqVersion = 2;

int parseArguments(int argc, char **argv) {
    char *s = NULL;
    int   c;
    
    int this_option_optind = optind;
    while (1) {
        this_option_optind = optind ? optind : 1;
        int option_index = 0;
        static struct option long_options[] = {
            {"help",           no_argument,          0, 'h'},
            {"start",          required_argument,    0, 's'},
            {"count",          required_argument,    0, 'c'},
            {"output",         required_argument,    0, 'o'},
            {0,                0,                    0, 0}
        };
        
        c = getopt_long(argc, argv, "c:s:o:f:hV", long_options, &option_index);
        if (c == -1) {
            break;
        }
        
        switch (c) {
            case 'c':
                channelCount = strtol(optarg, NULL, 10);
                break;
            case 's':
                startChannel = strtol(optarg, NULL, 10);
                break;
            case 'f':
                fseqVersion = strtol(optarg, NULL, 10);
                break;
            case 'o':
                outputFilename = optarg;
                break;
            case 'V':   printVersionInfo();
                exit(0);
            case 'h':    usage(argv[0]);
                exit(EXIT_SUCCESS);
            default:     usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    return this_option_optind;
}

int main(int argc, char *argv[]) {
    int idx = parseArguments(argc, argv);
    printf("Processing %s to %s using range %d-%d\n", argv[idx], outputFilename, startChannel, startChannel+channelCount);
    
    FSEQFile *src = FSEQFile::openFSEQFile(argv[idx]);
    if (src) {
        FSEQFile *dest = FSEQFile::createFSEQFile(outputFilename, fseqVersion,
                                                src->m_seqNumFrames,
                                                src->m_seqStartChannel,
                                                src->m_seqChannelCount,
                                                src->m_seqStepTime,
                                                src->m_variableHeaders);
        
        std::vector<std::pair<uint32_t, uint32_t>> ranges;
        ranges.push_back(std::pair<uint32_t, uint32_t>(startChannel, channelCount));
        uint8_t data[1024*1024];
        for (int x = 0; x < src->m_seqNumFrames; x++) {
            src->readFrame(x, data, ranges);
            dest->addFrame(x, data);
        }
        dest->finalize();
        delete dest;
        delete src;
    }

    
    return 0;
}
