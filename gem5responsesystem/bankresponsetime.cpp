/*
This is the mapping updated based on the static information.
Here the static information is the number of total requests per bank.
Not considering the traffic in the bank.
*/

#include <iostream>
#include <fstream>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <queue>
#include <algorithm>
#include <functional>
#include <numeric>
#include <string>
#include <map>

using namespace std;

#define PageHitDelay 20
#define PageMissDelay 200
// In cycles
#define readFwQueueTransferDelay 2
#define readBwQueueTransferDelay 8
#define writeFwQueueTransferDelay 8
#define writeBwQueueTransferDelay 2

#define numOfChannel 2
#define numOfRankInEachChannel 2
#define numOfBankInEachRank 2
#define numOfPartsInEachBank 4
#define numRows 16
#define numCols 256

#define channelBits 1
#define rankBits 1
#define bankBits 1
#define rowBits 4
#define colBits 8

#define bankQSize 32

int maxAddr = 1 << 15;
long long clk = 0;

long long maxRankQSize = 0;
long long maxChannelQSize = 0;

/**
 * Request structure to hold the type of the request and the slicing of the request address.
*/
struct request {
    int address;
    int RW; // 0 - Read, 1 - Write
    int channel; // Channel bits
    int rank; // Rank bits
    int bank; // Bank Bits
    int part; // The part - Not part of address bits (calculated using row)
    int row; // Row address bits
    int col; // col address bits
    int arrivalTime;
    int departureTime;
    int rClk;
    int localAT;

    request() : RW(0), channel(0) {}
};



ofstream MTF("DRAM_Queue_Simulation.txt");


class Bank {
	int Address;
	int RowBufferAddess;
	int PageSize, NumRowPerBank, NumColumnPerRow;
	int PageHit, PageMiss;
	int ReadCtr, WriteCtr;
    // Request counter to count number of requests in the bank
    long long requestCnt;

	public:


	Bank() {}

	void init(int r, int c) {
		NumRowPerBank = r;
		NumColumnPerRow = c;
		PageSize = NumColumnPerRow;
        requestCnt = 0;
	}

    // Accessing the specific row of the bank
	int Access(request* addr) {
        requestCnt++;

        if (addr->RW) WriteCtr++; 
        else ReadCtr++;

		if (RowBufferAddess == addr->row) {
			PageHit++;
            return PageHitDelay;
		}
		PageMiss++;
        RowBufferAddess = addr->row;
		return PageMissDelay;
	}


    int getNumPageHits() { return PageHit; }
	int getNumPageMisses() { return PageMiss; }
	int getNumWrite() { return WriteCtr; }
	int getNumRead() { return ReadCtr; }

    long long getNumRequest() { return requestCnt; }
    void resetRequestCnt() { requestCnt = 0; }
};


class Rank {
    int noOfBanks = 0, noOfRows = 0, noOfCols = 0;

    public:
    Bank* banks;

    Rank() {}

    void init(int b, int rows, int cols) {
        noOfBanks = b;
        noOfRows = rows;
        noOfCols = cols;

        banks = new Bank[noOfBanks];

        for (int i = 0; i < noOfBanks; i++) {
            banks[i].init(noOfRows, noOfCols);
        }
    }
};


class Channel {
    int noOfRanks = 0, noOfBanks = 0, noOfRows = 0, noOfCols = 0;

    public:
    Rank* ranks;

    Channel() {}

    void init(int r, int b, int rows, int cols) {
        noOfRanks = r;
        noOfBanks = b;
        noOfRows = rows;
        noOfCols = cols;

        ranks = new Rank[noOfRanks];

        for (int i = 0; i < noOfRanks; i++) {
            ranks[i].init(b, rows, cols);
        }
    }

};


class Memory {
    int noOfChannels, noOfRanks, noOfBanks, noOfParts, noOfRows, noOfCols;

    public:
    Channel* channels;

    map<vector<int>, vector<int>> mapping;

    Memory(int c, int r, int b, int p, int rows, int cols) {
        noOfChannels = c;
        noOfRanks = r;
        noOfBanks = b;
        noOfRows = rows;
        noOfCols = cols;
        noOfParts = p;

        channels = new Channel[noOfChannels];
        for (int i = 0; i < noOfChannels; i++) {
            channels[i].init(noOfRanks, noOfBanks, noOfRows, noOfCols);
        }
        generateInitialMapping();
    }

    // As of now generating the flat mapping. 
    // Going forward it will read from the configuration file.
    void generateInitialMapping() {
        // Genrating flat mapping 
        for (int c = 0; c < noOfChannels; c++) {
            for (int r = 0; r < noOfRanks; r++) {
                for (int b = 0; b < noOfBanks; b++) {
                    for (int p = 0; p < noOfParts; p++) {
                        vector<int> tmp = {c, r, b, p};
                        mapping[tmp] = tmp;
                    }
                }
            }
        }
    }


    int extractBits(int address, int endBit, int startBit) {
        int mask = (1 << (endBit - startBit + 1)) - 1;
        return (address >> startBit) & mask;
    }

    request* sliceAddress(int address) {
        request* r = new request();
        r->address = address;

        int start = 0, end = colBits-1;
        r->col = extractBits(address, end, start); // Col bits

        start = end+1; end = start + rowBits - 1;
        r->row = extractBits(address, end, start); // Row Bits

        r->part = (r->row / numOfPartsInEachBank);

        start = end+1; end = start + bankBits - 1;
        r->bank = extractBits(address, end, start); // Bank bits

        start = end+1; end = start + rankBits - 1;
        r->rank = extractBits(address, end, start);

        start = end+1; end = start + channelBits - 1;
        r->channel = extractBits(address, end, start);

        return r;
    }


    void mappingRequest(request** r) {
        vector<int> tmp = {(*r)->channel, (*r)->rank, (*r)->bank, (*r)->part};

        // cout << "Checking the appropriate mapping: ";
        // for (auto i: tmp) cout << i << ", ";
        // cout << endl;

        auto itr = mapping[tmp];
        (*r)->channel = itr[0];
        (*r)->rank = itr[1];
        (*r)->bank = itr[2];
        (*r)->part = itr[3];
    }


    int Access(int address, int RW) {
        // Read the backward queues from the channels
        request* r = sliceAddress(address);

        // Updating the request mapping
        mappingRequest(&r);

        int responseTime = 0;
        // Write
        if (r->RW) {
            responseTime += 3 * writeFwQueueTransferDelay + 3 * writeBwQueueTransferDelay;
        } else {
            responseTime += 3 * readFwQueueTransferDelay + 3 * readBwQueueTransferDelay;
        }
        responseTime += channels[r->channel].ranks[r->rank].banks[r->bank].Access(r);

        return responseTime;
    }

    void collectStats() {
        int TotalPageHit = 0, TotalPageMiss = 0;
		int TotalWrite = 0, TotalRead = 0;
		for (int i = 0;i < noOfChannels; i++) {
            for (int j = 0; j < noOfRanks; j++) {
                for (int k = 0; k < noOfBanks; k++) {
                    TotalPageHit += channels[i].ranks[j].banks[k].getNumPageHits();
                    TotalPageMiss += channels[i].ranks[j].banks[k].getNumPageMisses();
                    TotalWrite += channels[i].ranks[j].banks[k].getNumWrite();
                    TotalRead += channels[i].ranks[j].banks[k].getNumRead();
                }
            }
		}
		cout << "\n------------ Memory Statistics-----------\n";
        cout << "=> Total Page Hits = " << TotalPageHit << "\n=> Total Page Miss = " << TotalPageMiss;
		cout << "\n=> Total Reads = " << TotalRead << "\n=> Total Writes = " << TotalWrite<<"\n";
    }
};

// Main Memory
Memory* m = new Memory(numOfChannel, numOfRankInEachChannel, numOfBankInEachRank, numOfPartsInEachBank, numRows, numCols);

int gem5EntryExit(int address, int RW) {
    return m->Access(address, RW);
}


int main() {
    return 0;
}