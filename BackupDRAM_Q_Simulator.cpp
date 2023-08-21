#include <iostream>
#include <fstream>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <vector>

using namespace std;

#define HIT 1
#define MISS 0
#define READ 0
#define WRITE 1
#define PageHitDelay 50
#define PageMissDelay 600
// In cycles
#define readFwQueueTransferDelay 2
#define readBwQueueTransferDelay 8
#define writeFwQueueTransferDelay 8
#define writeBwQueueTransferDelay 2

#define numOfChannel 2
#define numOfRankInEachChannel 2
#define numOfBankInEachRank 2
#define numRows 16
#define numCols 256

#define channelBits 1
#define rankBits 1
#define bankBits 1
#define rowBits 4
#define colBits 8

int maxAddr = 1 << 15;

/**
 * Request structure to hold the type of the request and the slicing of the request address.
*/
struct request {
    int address;
    int RW; // 0 - Read, 1 - Write
    int channel; // Channel bits
    int rank; // Rank bits
    int bank; // Bank Bits
    int row; // Row address bits
    int col; // col address bits
    int arrivalTime;
    int departureTime;
    int rClk;

    request() : RW(0), channel(0) {}
};



ofstream MTF("DRAM_Queue_Simulation.txt");


// vector<request*> q;
/*
Queue will store the address and the clock ticking
When the request arrived and departed from the different queue.
1st element in the pair is the address
2nd element in the pair is another pair
    -> containing the arrival time and departure time of the request
*/


class Bank {
	int Address;
	int RowBufferAddess;
	int PageSize, NumRowPerBank, NumColumnPerRow;
	int PageHit, PageMiss;
	int ReadCtr, WriteCtr;

    int noOfRows, noOfCols;

	public:

    vector<request*> readForwardQueue;
    vector<request*> writeForwardQueue;
    vector<request*> readBackwardQueue;
    vector<request*> writeBackwardQueue;


	Bank() {}

	void init(int r, int c) {
        noOfRows = r;
        noOfCols = c;
		NumRowPerBank = r;
		NumColumnPerRow = c;
		PageSize = NumColumnPerRow;
	}

    // Accessing the specific row of the bank
	int Access(request* addr) {

        // cout << "One Access in the Bank: " << addr.address << endl;

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


    /**
     * Reads the forward queues and address the request.
     * As of now reading both the read and write queue
     * But only one can be done.
    */
    void updateQueues() {
        // Reading the read-forward-queue
        for (int i = 0; i < readForwardQueue.size(); i++) {
            readForwardQueue[i]->rClk--;
            if (readForwardQueue[i]->rClk == 0) {
                int delay = Access(readForwardQueue[i]) + readBwQueueTransferDelay;

                readForwardQueue[i]->rClk = delay;

                readBackwardQueue.push_back(readForwardQueue[i]);
                
                readForwardQueue.erase(readForwardQueue.begin() + i);
            }
        }

        // Reading the write-forward-queue
        for (int i = 0; i < writeForwardQueue.size(); i++) {
            writeForwardQueue[i]->rClk--;
            if (writeForwardQueue[i]->rClk == 0) {
                int delay = Access(writeForwardQueue[i]) + writeBwQueueTransferDelay;

                writeForwardQueue[i]->rClk = delay;

                writeBackwardQueue.push_back(writeForwardQueue[i]);
                
                writeForwardQueue.erase(writeForwardQueue.begin() + i);
            }
        }
    }
};



class Rank {
    int noOfBanks = 0, noOfRows = 0, noOfCols = 0;
    Bank* banks;

    public:
    vector<request*> readForwardQueue;
    vector<request*> writeForwardQueue;
    vector<request*> readBackwardQueue;
    vector<request*> writeBackwardQueue;

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

    /**
     * In the rank,
     * for each bank, it will check if the backward queue has anything.
     * If it has then it will transfer from there to rank's backward queue.
     * Then it will go to add the requests from the current forward queues to appropriate bank's queue
    */
    void updateQueues() {
        // Read the backward queues.
        for (int j = 0; j < noOfBanks; j++) {
            // If the ith bank has something in the backward queue
            for (int i = 0; i < banks[j].readBackwardQueue.size(); i++) {
                banks[j].readBackwardQueue[i]->rClk--;
                if (banks[j].readBackwardQueue[i]->rClk == 0) {
                    banks[j].readBackwardQueue[i]->rClk = readBwQueueTransferDelay;

                    readBackwardQueue.push_back(banks[j].readBackwardQueue[i]);

                    banks[j].readBackwardQueue.erase(banks[j].readBackwardQueue.begin() + i);
                }
            }

            for (int i = 0; i < banks[j].writeBackwardQueue.size(); i++) {
                banks[j].writeBackwardQueue[i]->rClk--;
                if (banks[j].writeBackwardQueue[i]->rClk == 0) {
                    banks[j].writeBackwardQueue[i]->rClk = readBwQueueTransferDelay;
                    writeBackwardQueue.push_back(banks[j].writeBackwardQueue[i]);
                    banks[j].writeBackwardQueue.erase(banks[j].writeBackwardQueue.begin() + i);
                }
            }

            banks[j].updateQueues();
        }

        // Check the forward queues if any request is ready to move forward

        for (int i = 0; i < readForwardQueue.size(); i++) {
            readForwardQueue[i]->rClk--;
            // Time to move it to next queue
            if (readForwardQueue[i]->rClk == 0) {
                readForwardQueue[i]->rClk = readFwQueueTransferDelay;
                banks[readForwardQueue[i]->bank].readForwardQueue.push_back(readForwardQueue[i]);

                readForwardQueue.erase(readForwardQueue.begin() + i);
            }
        }
        
        for (int i = 0; i < writeForwardQueue.size(); i++) {
            writeForwardQueue[i]->rClk--;
            // Time to move it to next queue
            if (writeForwardQueue[i]->rClk == 0) {
                writeForwardQueue[i]->rClk = writeFwQueueTransferDelay;
                banks[writeForwardQueue[i]->bank].writeForwardQueue.push_back(writeForwardQueue[i]);

                writeForwardQueue.erase(writeForwardQueue.begin() + i);
            }
        }
    }

};

class Channel {
    int noOfRanks = 0, noOfBanks = 0, noOfRows = 0, noOfCols = 0;
    Rank* ranks;

    public:

    vector<request*> readForwardQueue;
    vector<request*> writeForwardQueue;
    vector<request*> readBackwardQueue;
    vector<request*> writeBackwardQueue;

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

    void updateQueues() {
        // Read the backward queues.
        for (int j = 0; j < noOfBanks; j++) {
            // If the ith rank has something in the backward queue
            for (int i = 0; i < ranks[j].readBackwardQueue.size(); i++) {
                ranks[j].readBackwardQueue[i]->rClk--;
                if (ranks[j].readBackwardQueue[i]->rClk == 0) {
                    ranks[j].readBackwardQueue[i]->rClk = readBwQueueTransferDelay;

                    readBackwardQueue.push_back(ranks[j].readBackwardQueue[i]);

                    ranks[j].readBackwardQueue.erase(ranks[j].readBackwardQueue.begin() + i);
                }
            }

            for (int i = 0; i < ranks[j].writeBackwardQueue.size(); i++) {
                ranks[j].writeBackwardQueue[i]->rClk--;
                if (ranks[j].writeBackwardQueue[i]->rClk == 0) {
                    ranks[j].writeBackwardQueue[i]->rClk = readBwQueueTransferDelay;
                    writeBackwardQueue.push_back(ranks[j].writeBackwardQueue[i]);
                    ranks[j].writeBackwardQueue.erase(ranks[j].writeBackwardQueue.begin() + i);
                }
            }
            ranks[j].updateQueues();
        }

        for (int i = 0; i < readForwardQueue.size(); i++) {
            readForwardQueue[i]->rClk--;
            // Time to move it to next queue
            if (readForwardQueue[i]->rClk == 0) {
                readForwardQueue[i]->rClk = readFwQueueTransferDelay;
                ranks[readForwardQueue[i]->rank].readForwardQueue.push_back(readForwardQueue[i]);

                readForwardQueue.erase(readForwardQueue.begin() + i);
            }
        }
        
        for (int i = 0; i < writeForwardQueue.size(); i++) {
            writeForwardQueue[i]->rClk--;
            // Time to move it to next queue
            if (writeForwardQueue[i]->rClk == 0) {
                writeForwardQueue[i]->rClk = writeFwQueueTransferDelay;
                ranks[writeForwardQueue[i]->rank].writeForwardQueue.push_back(writeForwardQueue[i]);

                writeForwardQueue.erase(writeForwardQueue.begin() + i);
            }
        }
    }

};

class Memory {
    int noOfChannels, noOfRanks, noOfBanks, noOfRows, noOfCols;
    Channel* channels;

    public:

    Memory() {}

    void init(int c, int r, int b, int rows, int cols) {
        noOfChannels = c;
        noOfRanks = r;
        noOfBanks = b;
        noOfRanks = rows;
        noOfCols = cols;

        channels = new Channel[noOfChannels];
        for (int i = 0; i < noOfChannels; i++) {
            channels[i].init(noOfRanks, noOfBanks, noOfRows, noOfCols);
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

        start = end+1; end = start + bankBits - 1;
        r->bank = extractBits(address, end, start); // Bank bits

        start = end+1; end = start + rankBits - 1;
        r->rank = extractBits(address, end, start);

        start = end+1; end = start + channelBits - 1;
        r->channel = extractBits(address, end, start);

        return r;
    }

    void Access(int address, int RW, int clk, bool req) {
        // Read the backward queues from the channels
        for (int j = 0; j < noOfChannels; j++) {
            // cout << "Checking the backward queues for the channels: " << i << endl;
            // Check the read queue
            for (int i = 0; i < channels[j].readBackwardQueue.size(); i++) {
                channels[j].readBackwardQueue[i]->rClk--;
                if (channels[j].readBackwardQueue[i]->rClk == 0) {
                    MTF << hex << channels[j].readBackwardQueue[i]->address << dec << " : " << channels[j].readBackwardQueue[i]->RW << " Arrival clock: " << channels[j].readBackwardQueue[i]->arrivalTime << " Departure clock: " << clk << endl;
                    channels[j].readBackwardQueue.erase(channels[j].readBackwardQueue.begin() + i);
                }
            }
            
            for (int i = 0; i < channels[j].writeBackwardQueue.size(); i++) {
                channels[j].writeBackwardQueue[i]->rClk--;
                if (channels[j].writeBackwardQueue[i]->rClk == 0) {
                    MTF << hex << channels[j].writeBackwardQueue[i]->address << dec << " : " << channels[j].writeBackwardQueue[i]->RW << " Arrival clock: " << channels[j].writeBackwardQueue[i]->arrivalTime << " Departure clock: " << clk << endl;
                    channels[j].writeBackwardQueue.erase(channels[j].writeBackwardQueue.begin() + i);
                }
            }
            

            // Call updateQueue function on all the channels
            channels[j].updateQueues();
        }

        if (req) {
            request* r = sliceAddress(address);
            r->RW = RW;
            r->arrivalTime = clk;
            if (RW) {
                r->rClk = writeFwQueueTransferDelay;
                channels[r->channel].writeForwardQueue.push_back(r);
            } else {
                r->rClk = readFwQueueTransferDelay;
                channels[r->channel].readForwardQueue.push_back(r);
            }
        }
    }


};


void DRAMSim(int clk, Memory* m) {
    if (clk == 2 || clk == 4) {
        int addr = rand() % maxAddr;
        m->Access(addr, 0, clk, true);
    } else {
        m->Access(1, 0, clk, false);
    }
}


int main() {
	srand(time(NULL));
    int maxAddr = 1 << 15;

    Memory* m = new Memory();
    m->init(numOfChannel, numOfRankInEachChannel, numOfBankInEachRank, numRows, numCols);

    // int clock;
    // for (clock = 0; clock <= 50; clock += 5) {
    //     int addr = rand() % maxAddr;
        
    //     // if (rand() % 100 < 20)
    //     //     m.Access(addr, 1, clock); // Write operation
    //     // else
    //         m.Access(addr, 0, clock); // Read operation
    // }


    int clk = 0;
    while (clk < 1e3) {
        clk++;
        DRAMSim(clk, m);
    }

    return 0;
}